// StageDraw.h -- a cell's content drawn with QPainter, as the game draws it (game_scene_draw.cpp): palette icons, the
// object window's preview, the brush icon, the canvas's hover ghost, and the whole canvas when no GPU is there.
#pragma once

#include "stage_project.h"

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;
class SpriteBank;

namespace StageDraw {

QString spriteName(const stage::Json& v);   // a look's "sprite" ("slime.png" -> "slime"); "" when none
// The sprite a look draws with: its own when the atlas has it, else the semantic's usual one.
QString lookSprite(const stage::Project& p, const SpriteBank& bank, const std::string& sem, const stage::Json& look);
bool lookTint(const stage::Json& look, float rgba[4]);   // false: no tint (white)
// a gate: its key's colour at 60% over the door
bool gateWash(const stage::Project& p, const std::string& sem, float rgba[4]);
QColor kindColor(const stage::Project& p, const std::string& kind);

// The event's story plate (gold with a slate inset), a sprite with its look, or a coloured stand-in square.
void drawSem(QPainter& g, const stage::Project& p, const SpriteBank& bank, const std::string& sem, const QRectF& r,
             const stage::Json& look = stage::Json::object(), qreal opacity = 1.0);
// A palette icon: the floor under it (objects), then the type's look.
QImage icon(const stage::Project& p, const SpriteBank& bank, const std::string& sem, int size);

}  // namespace StageDraw

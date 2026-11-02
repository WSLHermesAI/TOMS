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

QString f_SpriteName(const stage::Json& l_jV);   // a look's "sprite" ("slime.png" -> "slime"); "" when none
// The sprite a look draws with: its own when the atlas has it, else the semantic's usual one.
QString f_LookSprite(const stage::Project& l_oP, const SpriteBank& l_oBank, const std::string& l_strSem, const stage::Json& l_jLook);
bool f_LookTint(const stage::Json& l_jLook, float l_arrRgba[4]);   // false: no tint (white)
// a gate: its key's colour at 60% over the door
bool f_GateWash(const stage::Project& l_oP, const std::string& l_strSem, float l_arrRgba[4]);
QColor f_KindColor(const stage::Project& l_oP, const std::string& l_strKind);

// The event's story plate (gold with a slate inset), a sprite with its look, or a coloured stand-in square.
void f_DrawSem(QPainter& l_oG, const stage::Project& l_oP, const SpriteBank& l_oBank, const std::string& l_strSem, const QRectF& l_oR,
             const stage::Json& l_jLook = stage::Json::object(), qreal l_dOpacity = 1.0);
// A palette icon: the floor under it (objects), then the type's look.
QImage f_Icon(const stage::Project& l_oP, const SpriteBank& l_oBank, const std::string& l_strSem, int l_iSize);

}  // namespace StageDraw

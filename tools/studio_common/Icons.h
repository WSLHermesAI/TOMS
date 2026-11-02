#pragma once

#include <QIcon>

class QImage;

// Every icon the TOMS editors use (tools/studio_common), drawn with QPainter so the tool ships without binary assets.
// Colours are mid-tones that read on both the dark and the light theme.
namespace Icons {

enum class Id {
    New, Open, Save, Import, Export, EditMode, ZoomIn, ZoomOut, Fit,
    Folder, Image, Child, Pin, Grid, Bake, Exclude,
    Add, Remove, Up, Down, Play, Pause, Back,
    Error, Warning, Info,
    // anim editor
    Move, Rotate, Scale, KeyOn, KeyOff, KeyNone, Eye, EyeOff, Stop, Loop, Node, SpriteNode,
    Duplicate, Event, Rename, AutoKey, Snap, Forward, Solo, Brush
};

QIcon f_Icon(Id l_eId);

// A sprite thumbnail as a tree icon: the image centred in a square, with a dashed frame for
// child sprites and a pin badge for pinned ones (what the Sprites dock shows).
QIcon f_SpriteIcon(const QImage& l_oThumb, bool l_bChild, bool l_bPinned, bool l_bExcluded);

}  // namespace Icons

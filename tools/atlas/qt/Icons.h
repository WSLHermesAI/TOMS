#pragma once

#include <QIcon>

class QImage;

// Every icon the editor uses, drawn with QPainter so the tool ships without binary assets.
// Colours are mid-tones that read on both the dark and the light theme.
namespace Icons {

enum class Id {
    New, Open, Save, Import, Export, EditMode, ZoomIn, ZoomOut, Fit,
    Folder, Image, Child, Pin, Grid, Bake, Exclude,
    Add, Remove, Up, Down, Play, Pause, Back,
    Error, Warning, Info
};

QIcon icon(Id id);

// A sprite thumbnail as a tree icon: the image centred in a square, with a dashed frame for
// child sprites and a pin badge for pinned ones (what the Sprites dock shows).
QIcon spriteIcon(const QImage& thumb, bool child, bool pinned, bool excluded);

}  // namespace Icons

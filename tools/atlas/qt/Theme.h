#pragma once

#include <QColor>

// Application look: Fusion with a dark (default) or light palette, plus the few colours the
// canvases draw with. The canvases read Theme::colors() on every paint, so switching the theme
// only needs a repaint.
namespace Theme {

enum class Mode { Dark, Light };

struct Colors {
    QColor canvasBackground;   // around the page
    QColor checkerLight, checkerDark;
    QColor pageBorder;
    QColor imageOutline;       // image sprites (solid)
    QColor childOutline;       // child sprites (dashed)
    QColor aliasOutline;       // dedupe aliases
    QColor selection;
    QColor hover;
    QColor pin;
    QColor pivot;
    QColor slice;              // 9-slice guides
    QColor label;              // text drawn over the canvas
    QColor labelBackground;
};

void apply(Mode mode);
Mode mode();
const Colors& colors();

}  // namespace Theme

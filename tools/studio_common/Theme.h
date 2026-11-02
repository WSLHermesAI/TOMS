#pragma once

#include <QColor>

// Application look of the TOMS editors (tools/studio_common): Fusion with a dark (default) or
// light palette, plus the few colours the canvases draw with. The canvases read Theme::colors() on every paint, so switching the theme
// only needs a repaint.
namespace Theme {

enum class Mode { Dark, Light };

struct Colors {
    QColor m_oCanvasBackground;   // around the page
    QColor m_oCheckerLight, m_oCheckerDark;
    QColor m_oPageBorder;
    QColor m_oImageOutline;       // image sprites (solid)
    QColor m_oChildOutline;       // child sprites (dashed)
    QColor m_oAliasOutline;       // dedupe aliases
    QColor m_oSelection;
    QColor m_oHover;
    QColor m_oPin;
    QColor m_oPivot;
    QColor m_oSlice;              // 9-slice guides
    QColor m_oLabel;              // text drawn over the canvas
    QColor m_oLabelBackground;
    // anim editor viewport
    QColor m_oGrid;               // minor grid lines
    QColor m_oGridAxis;           // the origin cross
    QColor m_oGizmoX, m_oGizmoY;     // move / scale handles along x and y
    QColor m_oGizmoRotate;
    QColor m_oKey;                // key markers (scrubber ticks, key indicators)
};

void f_Apply(Mode l_eMode);
Mode f_Mode();
const Colors& f_Colors();

}  // namespace Theme

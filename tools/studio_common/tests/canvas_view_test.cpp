// canvas_view_test.cpp -- the editors' canvas base (CanvasView): the coordinate readout follows the mouse, offscreen
// (QT_QPA_PLATFORM=offscreen; no window on the screen, no real mouse). A simulated mouse move over a test canvas
// must draw the x / y tags on the bottom and left edges and the label next to the cursor; leaving the canvas must
// remove them; with View > Show Coordinates off, nothing is drawn.
#include "CanvasView.h"
#include "Theme.h"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include <cstdio>

static int g_iChecks = 0, g_iFailed = 0;
#define CHECK(cond, msg) do { ++g_iChecks; if (!(cond)) { ++g_iFailed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); } } while (0)

class TestCanvas : public CanvasView {
public:
    using CanvasView::CanvasView;
protected:
    QSize f_ContentSize() const override { return QSize(200, 150); }
    void f_PaintContent(QPainter& l_oP) override { f_PaintBackground(l_oP); }
};

// Pixels of `c` (within a small tolerance) inside a rectangle of the image.
static int f_CountColour(const QImage& l_oImg, const QRect& l_oR, const QColor& l_oC) {
    int l_iN = 0;
    const QRect l_oA = l_oR.intersected(l_oImg.rect());
    for (int l_iY = l_oA.top(); l_iY <= l_oA.bottom(); l_iY++)
        for (int l_iX = l_oA.left(); l_iX <= l_oA.right(); l_iX++) {
            const QColor l_oP = l_oImg.pixelColor(l_iX, l_iY);
            if (std::abs(l_oP.red() - l_oC.red()) < 8 && std::abs(l_oP.green() - l_oC.green()) < 8 && std::abs(l_oP.blue() - l_oC.blue()) < 8) l_iN++;
        }
    return l_iN;
}

int main(int l_iArgc, char** l_pArgv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication l_oApp(l_iArgc, l_pArgv);
    QApplication::setOrganizationName(QStringLiteral("TOMS-tests"));
    QApplication::setApplicationName(QStringLiteral("canvas_view_test"));
    CanvasView::f_SetCoordinatesShown(true);

    TestCanvas l_oView;
    l_oView.resize(400, 300);
    l_oView.show();
    QApplication::processEvents();
    const QColor l_oMark = Theme::f_Colors().m_oSelection;

    auto l_fnRender = [&] { QImage l_oImg(l_oView.size(), QImage::Format_ARGB32); l_oImg.fill(Qt::black); l_oView.render(&l_oImg); return l_oImg; };
    const QImage l_oBefore = l_fnRender();
    CHECK(f_CountColour(l_oBefore, QRect(0, 280, 400, 20), l_oMark) == 0, "no cursor tag before the mouse comes in");

    const QPointF l_oAt(150, 120);
    QMouseEvent l_oMove(QEvent::MouseMove, l_oAt, l_oView.mapToGlobal(l_oAt.toPoint()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&l_oView, &l_oMove);
    const QImage l_oImg = l_fnRender();
    if (l_iArgc > 1) l_oImg.save(QString::fromLocal8Bit(l_pArgv[1]));   // optional: the frame with the readout, to look at
    CHECK(f_CountColour(l_oImg, QRect(120, 280, 60, 20), l_oMark) > 30, "the x tag on the bottom edge, under the cursor");
    CHECK(f_CountColour(l_oImg, QRect(0, 108, 40, 24), l_oMark) > 30, "the y tag on the left edge, beside the cursor");
    int l_iChanged = 0;   // the label is translucent: look for the region it darkens, against the frame before
    for (int l_iY = 136; l_iY < 152; l_iY++)
        for (int l_iX = 166; l_iX < 200; l_iX++) l_iChanged += l_oImg.pixelColor(l_iX, l_iY) != l_oBefore.pixelColor(l_iX, l_iY);
    CHECK(l_iChanged > 300, "the x, y label next to the cursor");

    QEvent l_oLeave(QEvent::Leave);
    QApplication::sendEvent(&l_oView, &l_oLeave);
    CHECK(f_CountColour(l_fnRender(), QRect(0, 280, 400, 20), l_oMark) == 0, "the tags go when the mouse leaves");

    QApplication::sendEvent(&l_oView, &l_oMove);
    CanvasView::f_SetCoordinatesShown(false);
    CHECK(f_CountColour(l_fnRender(), QRect(0, 280, 400, 20), l_oMark) == 0, "nothing with Show Coordinates off");
    CanvasView::f_SetCoordinatesShown(true);

    std::printf("canvas_view_test: %d checks, %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}

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

static int g_checks = 0, g_failed = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); } } while (0)

class TestCanvas : public CanvasView {
public:
    using CanvasView::CanvasView;
protected:
    QSize contentSize() const override { return QSize(200, 150); }
    void paintContent(QPainter& p) override { paintBackground(p); }
};

// Pixels of `c` (within a small tolerance) inside a rectangle of the image.
static int countColour(const QImage& img, const QRect& r, const QColor& c) {
    int n = 0;
    const QRect a = r.intersected(img.rect());
    for (int y = a.top(); y <= a.bottom(); y++)
        for (int x = a.left(); x <= a.right(); x++) {
            const QColor p = img.pixelColor(x, y);
            if (std::abs(p.red() - c.red()) < 8 && std::abs(p.green() - c.green()) < 8 && std::abs(p.blue() - c.blue()) < 8) n++;
        }
    return n;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TOMS-tests"));
    QApplication::setApplicationName(QStringLiteral("canvas_view_test"));
    CanvasView::setCoordinatesShown(true);

    TestCanvas view;
    view.resize(400, 300);
    view.show();
    QApplication::processEvents();
    const QColor mark = Theme::colors().selection;

    auto render = [&] { QImage img(view.size(), QImage::Format_ARGB32); img.fill(Qt::black); view.render(&img); return img; };
    const QImage before = render();
    CHECK(countColour(before, QRect(0, 280, 400, 20), mark) == 0, "no cursor tag before the mouse comes in");

    const QPointF at(150, 120);
    QMouseEvent move(QEvent::MouseMove, at, view.mapToGlobal(at.toPoint()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    const QImage img = render();
    if (argc > 1) img.save(QString::fromLocal8Bit(argv[1]));   // optional: the frame with the readout, to look at
    CHECK(countColour(img, QRect(120, 280, 60, 20), mark) > 30, "the x tag on the bottom edge, under the cursor");
    CHECK(countColour(img, QRect(0, 108, 40, 24), mark) > 30, "the y tag on the left edge, beside the cursor");
    int changed = 0;   // the label is translucent: look for the region it darkens, against the frame before
    for (int y = 136; y < 152; y++)
        for (int x = 166; x < 200; x++) changed += img.pixelColor(x, y) != before.pixelColor(x, y);
    CHECK(changed > 300, "the x, y label next to the cursor");

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
    CHECK(countColour(render(), QRect(0, 280, 400, 20), mark) == 0, "the tags go when the mouse leaves");

    QApplication::sendEvent(&view, &move);
    CanvasView::setCoordinatesShown(false);
    CHECK(countColour(render(), QRect(0, 280, 400, 20), mark) == 0, "nothing with Show Coordinates off");
    CanvasView::setCoordinatesShown(true);

    std::printf("canvas_view_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}

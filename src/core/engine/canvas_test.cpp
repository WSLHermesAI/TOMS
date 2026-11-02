// canvas_test.cpp -- toms::canvasFor (canvas.h): the design canvas for a screen's size. Exits 0 on success.
#include "canvas.h"

#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_iFail++; } } while (0)

int main() {
    CHECK(f_CanvasFor(1024, 768) == (Canvas{1024, 768}), "a 4:3 window: the canvas the layouts were made for");
    CHECK(f_CanvasFor(2048, 1536) == (Canvas{1024, 768}), "the size of the screen does not matter, only its shape");
    CHECK(f_CanvasFor(1920, 1080) == (Canvas{1365, 768}) && !f_CanvasFor(1920, 1080).f_Portrait(), "16:9: wider, no bars");
    CHECK(f_CanvasFor(2400, 1080) == (Canvas{1707, 768}), "20:9 phone, landscape");
    CHECK(f_CanvasFor(1080, 2400) == (Canvas{768, 1707}) && f_CanvasFor(1080, 2400).f_Portrait(), "20:9 phone, portrait");
    CHECK(f_CanvasFor(768, 1024) == (Canvas{768, 1024}), "3:4 portrait");
    CHECK(f_CanvasFor(1000, 1000) == (Canvas{1024, 768}), "square: the 4:3 canvas (thin bars)");
    CHECK(f_CanvasFor(900, 1000) == (Canvas{768, 1024}), "a little taller than wide: the 3:4 canvas");
    CHECK(f_CanvasFor(5000, 1000) == (Canvas{2048, 768}), "very wide: capped");
    CHECK(f_CanvasFor(0, 0) == (Canvas{1024, 768}), "no screen yet: the default");
    if (g_iFail) { std::printf("canvas_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("canvas_test: all passed\n");
    return 0;
}

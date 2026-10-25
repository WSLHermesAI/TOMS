// canvas_test.cpp -- toms::canvasFor (canvas.h): the design canvas for a screen's size. Exits 0 on success.
#include "canvas.h"

#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    CHECK(canvasFor(1024, 768) == (Canvas{1024, 768}), "a 4:3 window: the canvas the layouts were made for");
    CHECK(canvasFor(2048, 1536) == (Canvas{1024, 768}), "the size of the screen does not matter, only its shape");
    CHECK(canvasFor(1920, 1080) == (Canvas{1365, 768}) && !canvasFor(1920, 1080).portrait(), "16:9: wider, no bars");
    CHECK(canvasFor(2400, 1080) == (Canvas{1707, 768}), "20:9 phone, landscape");
    CHECK(canvasFor(1080, 2400) == (Canvas{768, 1707}) && canvasFor(1080, 2400).portrait(), "20:9 phone, portrait");
    CHECK(canvasFor(768, 1024) == (Canvas{768, 1024}), "3:4 portrait");
    CHECK(canvasFor(1000, 1000) == (Canvas{1024, 768}), "square: the 4:3 canvas (thin bars)");
    CHECK(canvasFor(900, 1000) == (Canvas{768, 1024}), "a little taller than wide: the 3:4 canvas");
    CHECK(canvasFor(5000, 1000) == (Canvas{2048, 768}), "very wide: capped");
    CHECK(canvasFor(0, 0) == (Canvas{1024, 768}), "no screen yet: the default");
    if (g_fail) { std::printf("canvas_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("canvas_test: all passed\n");
    return 0;
}

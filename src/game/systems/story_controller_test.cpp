// story_controller_test.cpp — headless verification of the Story Controller (Milestone 3).
// Exits 0 on success, 1 on any CHECK failure. Run: ./story_controller_test
#include "story_controller.h"
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    MetaSaveData meta;
    CHECK(meta.currentBeat == 0, "fresh MetaSaveData starts at beat 0");
    CHECK(!hasStoryFlag(meta, "anything"), "fresh MetaSaveData has no story flags set");

    setStoryFlag(meta, "beat_02_tutorial_done");
    CHECK(hasStoryFlag(meta, "beat_02_tutorial_done"), "setStoryFlag then hasStoryFlag sees it");
    CHECK(!hasStoryFlag(meta, "beat_03_something"), "an unset flag is not seen as set");

    advanceStoryBeat(meta, 4);
    CHECK(meta.currentBeat == 4, "advanceStoryBeat(4) sets currentBeat to 4");

    advanceStoryBeat(meta, 2);
    CHECK(meta.currentBeat == 4, "advanceStoryBeat(2) does not regress currentBeat below 4 (monotonic)");

    advanceStoryBeat(meta, 4);
    CHECK(meta.currentBeat == 4, "advanceStoryBeat(4) again is a no-op at the same beat");

    advanceStoryBeat(meta, 7);
    CHECK(meta.currentBeat == 7, "advanceStoryBeat(7) advances forward correctly");

    setStoryFlag(meta, "beat_02_tutorial_done"); // re-setting an already-set flag must not duplicate it
    CHECK(meta.storyFlags.size() == 1, "re-setting an already-set flag doesn't duplicate it");

    if (g_fail == 0) { printf("story_controller_test: ALL PASS (9 checks)\n"); return 0; }
    printf("story_controller_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}

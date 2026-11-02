// story_controller_test.cpp — headless verification of the Story Controller (Milestone 3).
// Exits 0 on success, 1 on any CHECK failure. Run: ./story_controller_test
#include "story_controller.h"
#include <cstdio>

using namespace toms;

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_iFail++; } } while(0)

int main() {
    MetaSaveData l_oMeta;
    CHECK(l_oMeta.m_iCurrentBeat == 0, "fresh MetaSaveData starts at beat 0");
    CHECK(!f_HasStoryFlag(l_oMeta, "anything"), "fresh MetaSaveData has no story flags set");

    f_SetStoryFlag(l_oMeta, "beat_02_tutorial_done");
    CHECK(f_HasStoryFlag(l_oMeta, "beat_02_tutorial_done"), "setStoryFlag then hasStoryFlag sees it");
    CHECK(!f_HasStoryFlag(l_oMeta, "beat_03_something"), "an unset flag is not seen as set");

    f_AdvanceStoryBeat(l_oMeta, 4);
    CHECK(l_oMeta.m_iCurrentBeat == 4, "advanceStoryBeat(4) sets currentBeat to 4");

    f_AdvanceStoryBeat(l_oMeta, 2);
    CHECK(l_oMeta.m_iCurrentBeat == 4, "advanceStoryBeat(2) does not regress currentBeat below 4 (monotonic)");

    f_AdvanceStoryBeat(l_oMeta, 4);
    CHECK(l_oMeta.m_iCurrentBeat == 4, "advanceStoryBeat(4) again is a no-op at the same beat");

    f_AdvanceStoryBeat(l_oMeta, 7);
    CHECK(l_oMeta.m_iCurrentBeat == 7, "advanceStoryBeat(7) advances forward correctly");

    f_SetStoryFlag(l_oMeta, "beat_02_tutorial_done"); // re-setting an already-set flag must not duplicate it
    CHECK(l_oMeta.m_setStoryFlags.size() == 1, "re-setting an already-set flag doesn't duplicate it");

    if (g_iFail == 0) { printf("story_controller_test: ALL PASS (9 checks)\n"); return 0; }
    printf("story_controller_test: %d CHECK(s) FAILED\n", g_iFail);
    return 1;
}

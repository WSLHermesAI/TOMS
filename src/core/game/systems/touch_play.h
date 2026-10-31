// touch_play.h -- each weapon's own play (docs/gameplay/14 §2, 15 §3.3), the attack played by touch over the monster:
//   Bow      drag back and let go (a slingshot): the crosshair goes the other way; hit the target. One per arrow.
//   Slice    swipe the way the arrow shows (a sword's cut). One per cut. The ultimate: the arrows are shown, then hidden,
//            and a wrong way ends the chain (a memory combo).
//   Circles  tap each circle when its closing ring meets it (Ouendan). One per circle.
// Every play reports on the track's scale (combo_play.h's ComboResult: hits of points, failed), so the result rows,
// their particles and clips, the break gauge and the grade work the same for all. Its timers wait for the first touch
// (the owner, 2026-10-31: the play waits over the monster; no button starts it).
//
// Pure: positions in design pixels, time in ms, a seed for the random places; no Game dependency.
#pragma once
#include "combo_play.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace toms {

enum class PlayKind { Track, Bow, Slice, Circles };
PlayKind playKindFromString(const std::string& s);   // "bow", "slice", "circles"; anything else: the track
const char* playKindName(PlayKind k);

struct TouchPlaySettings {
    // bow
    int bowTimeMs = 3000;          // per arrow, once started
    float bowRadius = 32.0f;       // the target (the ultimate's: 0.8 of it)
    float bowPull = 1.8f;          // the crosshair moves this many times the pull
    float bowMinPull = 25.0f;      // a shorter pull is not a shot
    // slice
    int sliceTimeMs = 1600;        // per cut (the memory chain: x1.4)
    int sliceShowMs = 1400;        // the memory chain shows its arrows this long
    float sliceMinLength = 70.0f;  // a shorter swipe is not a cut
    float sliceTolerance = 0.5f;   // radians off the arrow's way that still cut
    // circles
    int circleFirstMs = 900;       // the first circle closes this long after the first tap
    int circleGapMs = 550;         // then one every this long (the ultimate: 0.7 of it)
    int circleWindowMs = 200;      // a tap this close in time hits...
    int circlePerfectMs = 80;      // ...this close is perfect
    float circleRadius = 34.0f;
};

class TouchPlay {
public:
    struct Target { float x = 0, y = 0, r = 0; };
    struct Arrow { int dir = 0; int state = 0; };                 // dir 0..7 (×45°, 0 = right, clockwise); 0 wait 1 cut 2 missed
    struct Circle { float x = 0, y = 0; int atMs = 0; int state = 0; };   // 0 wait 1 hit 2 perfect 3 missed
    struct Mark { float x = 0, y = 0; bool ok = false; int ageMs = 0; };

    // A new play of `points` over `area` (x, y, w, h: where its targets and circles may go). The ultimate starts at once.
    void start(PlayKind kind, int points, bool ultimate, const glm::vec4& area, uint32_t seed,
               const TouchPlaySettings& s = TouchPlaySettings());
    void stop() { running_ = false; }
    // The pointer: 0 down, 1 move (held), 2 up; design pixels.
    void pointer(int phase, float x, float y);
    void update(int dtMs);

    bool running() const { return running_ && !done_; }
    bool started() const { return started_; }
    bool done() const { return done_; }
    ComboResult result() const;
    PlayKind kind() const { return kind_; }
    bool ultimate() const { return ultimate_; }
    int index() const { return index_; }
    int points() const { return points_; }
    float timeLeft() const;                       // 0..1, the current arrow / cut (1 before the start)

    // what to draw
    const Target& target() const { return target_; }                       // bow
    bool aiming() const { return aiming_; }
    glm::vec2 aimFrom() const { return aimFrom_; }
    glm::vec2 aimTo() const { return aimTo_; }
    glm::vec2 crosshair() const;
    const std::vector<Arrow>& arrows() const { return arrows_; }           // slice
    bool arrowsShown() const;                                              // (the memory chain hides them)
    bool swiping() const { return aiming_ && kind_ == PlayKind::Slice; }
    const std::vector<Circle>& circles() const { return circles_; }       // circles
    int clockMs() const { return clockMs_; }
    const std::vector<Mark>& marks() const { return marks_; }

private:
    void nextTarget();
    void finishOne(bool ok, bool perfect, float x, float y);
    PlayKind kind_ = PlayKind::Track;
    TouchPlaySettings s_;
    glm::vec4 area_{0.0f};
    std::mt19937 rng_;
    bool running_ = false, started_ = false, done_ = false, ultimate_ = false, failed_ = false;
    int points_ = 0, index_ = 0, hits_ = 0, perfects_ = 0;
    int clockMs_ = 0, oneMs_ = 0;   // the play's time since the start; the current arrow / cut's
    Target target_;
    bool aiming_ = false;
    glm::vec2 aimFrom_{0.0f}, aimTo_{0.0f};
    std::vector<Arrow> arrows_;
    std::vector<Circle> circles_;
    std::vector<Mark> marks_;
};

} // namespace toms

// touch_play.h -- each weapon's own play, and the plays of moves and special guards (docs/gameplay/15 §3.3,
// docs/gameplay/16_SKILLS_AND_PLAYS.md §3, §6), built from the reusable blocks (cue_track.h, gesture.h):
//   Bow      the target moves a moment, then stops; touch anywhere, drag back and let go (a slingshot): the crosshair
//            goes the other way. Holding the pull charges the shot (up to a limit: then it goes) and shakes the aim.
//            A normal attack is one arrow; a move asks for several.
//   Slice    the sword's prompts, one at a time on a cue track: each pops in, and only its input window takes the
//            answer: a swipe the arrow's way, or a shape drawn (a triangle, a circle). Each answer is a hit at once.
//   Circles  the staff's circles: they count in order with their numbers, hide, come back together with an "!", and
//            are tapped in the order remembered. Each tap casts a spell at once.
//   Shape    a shape shown on the screen, traced corner by corner in a time (the staff's pentagram, a special guard).
//   Mash     tap quickly, `points` times in a time (a special guard).
// Every play reports on the track's scale (combo_play.h's ComboResult: hits of points, perfects, failed), so the
// result rows, their particles and clips, the break gauge and the grade work the same for all; takeHits() gives each
// answer as it happens, for the plays whose answers hit at once.
//
// Pure: positions in design pixels, time in ms, a seed for the random places; no Game dependency.
#pragma once
#include "combo_play.h"
#include "cue_track.h"
#include "gesture.h"

#include <glm/glm.hpp>
#include <json.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace toms {

enum class PlayKind { Track, Bow, Slice, Circles, Shape, Mash };
PlayKind playKindFromString(const std::string& s);   // "bow", "slice", "circles", "shape", "mash"; else the track
const char* playKindName(PlayKind k);

struct TouchPlaySettings {
    // bow
    int bowTimeMs = 4000;          // per arrow, once started
    float bowRadius = 32.0f;       // the target (a move's: 0.8 of it)
    float bowPull = 1.8f;          // the crosshair moves this many times the pull
    float bowMinPull = 25.0f;      // a shorter pull is not a shot
    int bowMoveMs = 500;           // a new target moves this long, then stops (the owner: 0.5 s)
    float bowMoveDist = 90.0f;     // ...this far
    int bowHoldMs = 1500;          // the pull's charge limit: held this long, full charge, and the arrow goes (Game: STR)
    float bowShakePx = 10.0f;      // the aim's shake after 1 s held, growing with the hold (Game: less with DEX)
    // slice
    CueTiming sliceCue;                    // each prompt 1000 ms, of which the last 500 ms take the answer
    float sliceMinLength = 70.0f;          // a shorter swipe is not a cut
    float sliceTolerance = 0.5f;           // radians off the arrow's way that still cut
    std::vector<std::string> sliceCues;    // each prompt: "way" or a shape name ("triangle", "circle"); empty: ways
    // circles
    int circleCountMs = 450;       // each circle lit in turn with its number
    int circleHideMs = 500;        // then all hidden (the owner: 0.5 s)
    int circleWarnMs = 500;        // then back together with an "!", not yet to be tapped (0.5 s)
    int circleTapMs = 900;         // then the time to tap them all: this much per circle
    int circlePerfectMs = 500;     // a tap this soon after the last answer is perfect
    float circleRadius = 34.0f;
    // shape
    std::string shape = "pentagram";
    float shapeSize = 220.0f;
    float shapeCornerRadius = 44.0f;
    int shapeTimeMs = 6000;
    int shapeStepPerfectMs = 1200; // a corner this soon after the last is perfect
    // mash
    int mashTimeMs = 2000;
    // the player's stats (Game applies them): STR lengthens the bow's charge limit, DEX calms its shake
    int bowHoldPerStr = 40;        // ms more per STR point
    float bowShakePerDex = 0.05f;  // the shake / (1 + DEX x this)
};

// data/battle.json "plays" (anything missing keeps its default above).
TouchPlaySettings touchPlayFromJson(const nlohmann::json& battleJson);

class TouchPlay {
public:
    struct Target { float x = 0, y = 0, r = 0; };
    struct Circle { float x = 0, y = 0; int state = 0; };   // 0 wait 1 hit 2 perfect 3 missed
    struct Mark { float x = 0, y = 0; bool ok = false; int ageMs = 0; };
    struct Hit { bool ok = false, perfect = false; float x = 0, y = 0; };   // one answer, as it happens
    enum class CirclePhase { Count, Hide, Warn, Tap };

    // A new play of `points` over `area` (x, y, w, h: where its targets and circles may go). A move (`ultimate`) and
    // the sword, staff, shape and mash plays start at once; the bow waits for its first touch.
    void start(PlayKind kind, int points, bool ultimate, const glm::vec4& area, uint32_t seed,
               const TouchPlaySettings& s = TouchPlaySettings());
    void stop() { running_ = false; }
    // The pointer: 0 down, 1 move (held), 2 up; design pixels.
    void pointer(int phase, float x, float y);
    void update(int dtMs);
    std::vector<Hit> takeHits() { std::vector<Hit> h; h.swap(hits_); return h; }

    bool running() const { return running_ && !done_; }
    bool started() const { return started_; }
    bool done() const { return done_; }
    ComboResult result() const;
    PlayKind kind() const { return kind_; }
    bool ultimate() const { return ultimate_; }
    int index() const { return index_; }
    int points() const { return points_; }
    float timeLeft() const;                       // 0..1: the current arrow, prompt, or the whole play (1 before the start)
    int clockMs() const { return clockMs_; }
    const std::vector<Mark>& marks() const { return marks_; }

    // bow
    const Target& target() const { return target_; }
    bool aiming() const { return aiming_; }
    glm::vec2 aimFrom() const { return aimFrom_; }
    glm::vec2 aimTo() const { return aimTo_; }
    glm::vec2 crosshair() const;                  // where the arrow would go now (with the shake)
    float charge() const;                         // the pull's charge now, 0..1
    float shotCharge() const { return shots_ > 0 ? chargeSum_ / (float)shots_ : 0.0f; }   // the shots' average
    // slice
    const CueTrack& cues() const { return cueTrack_; }
    const std::string& cueKind(int k) const { return cueKinds_[(size_t)k]; }   // "way" or a shape
    int cueDir(int k) const { return cueDirs_[(size_t)k]; }                      // a way: 0..7 (×45°, 0 = right, clockwise)
    glm::vec2 promptAt() const;                   // where the prompt shows
    bool swiping() const { return aiming_ && kind_ == PlayKind::Slice; }
    const std::vector<glm::vec2>& stroke() const { return stroke_; }
    // circles
    const std::vector<Circle>& circles() const { return circles_; }
    CirclePhase circlePhase() const { return circlePhase_; }
    int circlesLit() const;                       // the count: how many have shown their number so far
    // shape
    const ShapeTrace& trace() const { return trace_; }

private:
    void nextTarget();
    glm::vec2 targetPos() const;
    void finishOne(bool ok, bool perfect, float x, float y);
    void release();
    void judgeStroke();
    PlayKind kind_ = PlayKind::Track;
    TouchPlaySettings s_;
    glm::vec4 area_{0.0f};
    std::mt19937 rng_;
    bool running_ = false, started_ = false, done_ = false, ultimate_ = false, failed_ = false;
    int points_ = 0, index_ = 0, hitCount_ = 0, perfects_ = 0;
    int clockMs_ = 0, ageMs_ = 0, oneMs_ = 0, lastAnswerMs_ = 0;   // since the start; since armed; the current one's; the last answer
    std::vector<Mark> marks_;
    std::vector<Hit> hits_;
    // bow
    Target target_;
    glm::vec2 targetFrom_{0.0f}, targetTo_{0.0f};
    int targetAgeMs_ = 0, holdMs_ = 0, shots_ = 0;
    float chargeSum_ = 0.0f;
    bool aiming_ = false;
    glm::vec2 aimFrom_{0.0f}, aimTo_{0.0f};
    // slice
    CueTrack cueTrack_;
    std::vector<std::string> cueKinds_;
    std::vector<int> cueDirs_;
    std::vector<glm::vec2> stroke_;
    // circles
    std::vector<Circle> circles_;
    CirclePhase circlePhase_ = CirclePhase::Count;
    int phaseMs_ = 0;
    // shape
    ShapeTrace trace_;
};

} // namespace toms

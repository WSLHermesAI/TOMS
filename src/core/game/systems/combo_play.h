// combo_play.h -- Battle v3's point play (docs/gameplay/07_GAMEPLAY_PLAN.md §4, §5.1; phase 1, the owner 2026-10-22).
//
// While the attack waits, charge builds up (0 .. 1). The first tap starts the combo: a marker sweeps a track with the
// weapon level's points (1 .. 5), faster the more charge there was. Each tap close to a point not hit yet hits it, in
// any order. A tap that hits nothing is a miss: the combo goes back by the weapon's rule (weapon_play.h missBack), and
// the next taps wait a little (missWaitMs). Two misses in a row are a fail. The combo ends when every point is hit,
// after the sweeps run out (a weapon whose marker does not turn back has one sweep), or on a fail; its result is the
// hits, the points and the charge, from which the battle computes the damage.
//
// How close a tap must be (the owner 2026-10-24: "hard to get the timing right"): within the point's window, which is
// hitWindow of the track but never shorter than hitWindowMs of the marker's travel (so a full charge's faster marker
// does not halve it), and never more than 45% of the gap between two points (a tap still has to aim). A tap up to
// lateMs after the marker passed a point still counts (touch and display delay). nearPoint() / takeEntered() say when
// the marker is in a window, for the screen's hint.
//
// Pure logic, no Game dependency (systems/*). Times in ms; positions are fractions of the track (0 .. 1).
#pragma once
#include "weapon_play.h"

#include <vector>

namespace toms {

struct ComboSettings {
    int points = 3;
    bool turnBack = true;
    int sweepMs = 1600;          // one sweep with no charge
    int maxSweeps = 3;
    float speedUp = 1.0f;        // a full charge: this much faster
    float hitWindow = 0.07f;
    int hitWindowMs = 0;         // the window is at least this long in time (0: hitWindow only)
    int lateMs = 0;              // a tap this long after passing a point still hits it
    int missBack = -1;           // points a miss takes back; -1: to the start
    int missWaitMs = 0;
};
// The settings of a weapon at a level.
ComboSettings comboSettings(const WeaponPlay& w, int weaponLevel);

struct ComboResult {
    int hits = 0, points = 0;
    float charge = 0;            // 0 .. 1
    bool failed = false;         // two misses in a row (a plain attack)
    int perfects = -1;           // a touch play's perfect answers (touch_play.h); -1: not counted (the track)
    // The attack's power for the damage formula, 0 .. 100: the share of points hit (a fail: 0).
    float power() const { return failed || points <= 0 ? 0.0f : 100.0f * float(hits) / float(points); }
};

class ComboPlay {
public:
    enum class Tap { Hit, Miss, Ignored };

    // Starts a combo with this much charge (0 .. 1).
    void start(const ComboSettings& s, float charge);
    void update(int dtMs);
    Tap tap();
    void stop() { running_ = false; }

    bool running() const { return running_; }
    bool done() const { return done_; }            // ended; result() holds it (cleared by start())
    const ComboResult& result() const { return result_; }

    float marker() const { return pos_; }          // 0 .. 1
    const std::vector<float>& pointsAt() const { return at_; }
    const std::vector<bool>& pointsHit() const { return hit_; }
    int hits() const;
    int missesInRow() const { return missesInRow_; }
    bool waiting() const { return waitMs_ > 0; }   // after a miss: taps do not count yet
    float window() const;                          // a point's window now (each side, a fraction of the track)
    int nearPoint() const;                         // the point not hit yet whose window the marker is in (-1: none)
    std::vector<int> takeEntered();                // the points whose window the marker entered since the last call

private:
    void finish(bool failed);

    ComboSettings s_;
    float charge_ = 0, pos_ = 0, speed_ = 0;      // speed: track fractions per ms
    int dir_ = 1, sweeps_ = 0, waitMs_ = 0, missesInRow_ = 0;
    bool running_ = false, done_ = false;
    std::vector<float> at_;
    std::vector<bool> hit_;
    std::vector<int> order_;                       // the points hit, oldest first (a miss takes back the newest)
    int lastNear_ = -1;
    std::vector<int> entered_;
    ComboResult result_;
};

} // namespace toms

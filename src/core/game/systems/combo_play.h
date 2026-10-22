// combo_play.h -- Battle v3's point play (docs/30_GAMEPLAY_PLAN.md §4, §5.1; phase 1, the owner 2026-10-22).
//
// While the attack waits, charge builds up (0 .. 1). The first tap starts the combo: a marker sweeps a track with the
// weapon level's points (1 .. 5), faster the more charge there was. Each tap close to a point not hit yet hits it, in
// any order. A tap that hits nothing is a miss: the combo goes back by the weapon's rule (weapon_play.h missBack), and
// the next taps wait a little (missWaitMs). Two misses in a row are a fail. The combo ends when every point is hit,
// after the sweeps run out (a weapon whose marker does not turn back has one sweep), or on a fail; its result is the
// hits, the points and the charge, from which the battle computes the damage.
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
    int missBack = -1;           // points a miss takes back; -1: to the start
    int missWaitMs = 0;
};
// The settings of a weapon at a level.
ComboSettings comboSettings(const WeaponPlay& w, int weaponLevel);

struct ComboResult {
    int hits = 0, points = 0;
    float charge = 0;            // 0 .. 1
    bool failed = false;         // two misses in a row (a plain attack)
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

private:
    void finish(bool failed);

    ComboSettings s_;
    float charge_ = 0, pos_ = 0, speed_ = 0;      // speed: track fractions per ms
    int dir_ = 1, sweeps_ = 0, waitMs_ = 0, missesInRow_ = 0;
    bool running_ = false, done_ = false;
    std::vector<float> at_;
    std::vector<bool> hit_;
    std::vector<int> order_;                       // the points hit, oldest first (a miss takes back the newest)
    ComboResult result_;
};

} // namespace toms

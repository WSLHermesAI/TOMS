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
    int m_iPoints = 3;
    bool m_bTurnBack = true;
    int m_iSweepMs = 1600;          // one sweep with no charge
    int m_iMaxSweeps = 3;
    float m_fSpeedUp = 1.0f;        // a full charge: this much faster
    float m_fHitWindow = 0.07f;
    int m_iHitWindowMs = 0;         // the window is at least this long in time (0: hitWindow only)
    int m_iLateMs = 0;              // a tap this long after passing a point still hits it
    int m_iMissBack = -1;           // points a miss takes back; -1: to the start
    int m_iMissWaitMs = 0;
};
// The settings of a weapon at a level.
ComboSettings f_ComboSettings(const WeaponPlay& l_oW, int l_iWeaponLevel);

struct ComboResult {
    int m_iHits = 0, m_iPoints = 0;
    float m_fCharge = 0;            // 0 .. 1
    bool m_bFailed = false;         // two misses in a row (a plain attack)
    int m_iPerfects = -1;           // a touch play's perfect answers (touch_play.h); -1: not counted (the track)
    // The attack's power for the damage formula, 0 .. 100: the share of points hit (a fail: 0).
    float f_Power() const { return m_bFailed || m_iPoints <= 0 ? 0.0f : 100.0f * float(m_iHits) / float(m_iPoints); }
};

class ComboPlay {
public:
    enum class Tap { Hit, Miss, Ignored };

    // Starts a combo with this much charge (0 .. 1).
    void f_Start(const ComboSettings& l_oS, float l_fCharge);
    void f_Update(int l_iDtMs);
    Tap f_Tap();
    void f_Stop() { m_bRunning = false; }

    bool f_Running() const { return m_bRunning; }
    bool f_Done() const { return m_bDone; }            // ended; result() holds it (cleared by start())
    const ComboResult& f_Result() const { return m_oResult; }

    float f_Marker() const { return m_fPos; }          // 0 .. 1
    const std::vector<float>& f_PointsAt() const { return m_vAt; }
    const std::vector<bool>& f_PointsHit() const { return m_vHit; }
    int f_Hits() const;
    int f_MissesInRow() const { return m_iMissesInRow; }
    bool f_Waiting() const { return m_iWaitMs > 0; }   // after a miss: taps do not count yet
    float f_Window() const;                          // a point's window now (each side, a fraction of the track)
    int f_NearPoint() const;                         // the point not hit yet whose window the marker is in (-1: none)
    std::vector<int> f_TakeEntered();                // the points whose window the marker entered since the last call

private:
    void f_Finish(bool l_bFailed);

    ComboSettings m_oS;
    float m_fCharge = 0, m_fPos = 0, m_fSpeed = 0;      // speed: track fractions per ms
    int m_iDir = 1, m_iSweeps = 0, m_iWaitMs = 0, m_iMissesInRow = 0;
    bool m_bRunning = false, m_bDone = false;
    std::vector<float> m_vAt;
    std::vector<bool> m_vHit;
    std::vector<int> m_vOrder;                       // the points hit, oldest first (a miss takes back the newest)
    int m_iLastNear = -1;
    std::vector<int> m_vEntered;
    ComboResult m_oResult;
};

} // namespace toms

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
PlayKind f_PlayKindFromString(const std::string& l_strS);   // "bow", "slice", "circles", "shape", "mash"; else the track
const char* f_PlayKindName(PlayKind l_eK);

struct TouchPlaySettings {
    // bow
    int m_iBowTimeMs = 4000;          // per arrow, once started
    float m_fBowRadius = 32.0f;       // the target (a move's: 0.8 of it)
    float m_fBowPull = 1.8f;          // the crosshair moves this many times the pull
    float m_fBowMinPull = 25.0f;      // a shorter pull is not a shot
    int m_iBowMoveMs = 500;           // a new target moves this long, then stops (the owner: 0.5 s)
    float m_fBowMoveDist = 90.0f;     // ...this far
    int m_iBowHoldMs = 1500;          // the pull's charge limit: held this long, full charge, and the arrow goes (Game: STR)
    float m_fBowShakePx = 10.0f;      // the aim's shake after 1 s held, growing with the hold (Game: less with DEX)
    // slice
    CueTiming m_oSliceCue;                    // each prompt 1000 ms, of which the last 500 ms take the answer
    float m_fSliceMinLength = 70.0f;          // a shorter swipe is not a cut
    float m_fSliceTolerance = 0.5f;           // radians off the arrow's way that still cut
    std::vector<std::string> m_vSliceCues;    // each prompt: "way" or a shape name ("triangle", "circle"); empty: ways
    // circles
    int m_iCircleCountMs = 450;       // each circle lit in turn with its number
    int m_iCircleHideMs = 500;        // then all hidden (the owner: 0.5 s)
    int m_iCircleWarnMs = 500;        // then back together with an "!", not yet to be tapped (0.5 s)
    int m_iCircleTapMs = 900;         // then the time to tap them all: this much per circle
    int m_iCirclePerfectMs = 500;     // a tap this soon after the last answer is perfect
    float m_fCircleRadius = 34.0f;
    // shape
    std::string m_strShape = "pentagram";
    float m_fShapeSize = 220.0f;
    float m_fShapeCornerRadius = 44.0f;
    int m_iShapeTimeMs = 6000;
    int m_iShapeStepPerfectMs = 1200; // a corner this soon after the last is perfect
    // mash
    int m_iMashTimeMs = 2000;
    // the player's stats (Game applies them): STR lengthens the bow's charge limit, DEX calms its shake
    int m_iBowHoldPerStr = 40;        // ms more per STR point
    float m_fBowShakePerDex = 0.05f;  // the shake / (1 + DEX x this)
};

// data/battle.json "plays" (anything missing keeps its default above).
TouchPlaySettings f_TouchPlayFromJson(const nlohmann::json& l_jBattleJson);

class TouchPlay {
public:
    struct Target { float m_fX = 0, m_fY = 0, m_fR = 0; };
    struct Circle { float m_fX = 0, m_fY = 0; int m_iState = 0; };   // 0 wait 1 hit 2 perfect 3 missed
    struct Mark { float m_fX = 0, m_fY = 0; bool m_bOk = false; int m_iAgeMs = 0; };
    struct Hit { bool m_bOk = false, m_bPerfect = false; float m_fX = 0, m_fY = 0; };   // one answer, as it happens
    enum class CirclePhase { Count, Hide, Warn, Tap };

    // A new play of `points` over `area` (x, y, w, h: where its targets and circles may go). A move (`ultimate`) and
    // the sword, staff, shape and mash plays start at once; the bow waits for its first touch.
    void f_Start(PlayKind l_eKind, int l_iPoints, bool l_bUltimate, const glm::vec4& l_v4Area, uint32_t l_uSeed,
               const TouchPlaySettings& l_oS = TouchPlaySettings());
    void f_Stop() { m_bRunning = false; }
    // The pointer: 0 down, 1 move (held), 2 up; design pixels.
    void f_Pointer(int l_iPhase, float l_fX, float l_fY);
    void f_Update(int l_iDtMs);
    std::vector<Hit> f_TakeHits() { std::vector<Hit> l_vH; l_vH.swap(m_vHits); return l_vH; }

    bool f_Running() const { return m_bRunning && !m_bDone; }
    bool f_Started() const { return m_bStarted; }
    bool f_Done() const { return m_bDone; }
    ComboResult f_Result() const;
    PlayKind f_Kind() const { return m_eKind; }
    bool f_Ultimate() const { return m_bUltimate; }
    int f_Index() const { return m_iIndex; }
    int f_Points() const { return m_iPoints; }
    float f_TimeLeft() const;                       // 0..1: the current arrow, prompt, or the whole play (1 before the start)
    int f_ClockMs() const { return m_iClockMs; }
    const std::vector<Mark>& f_Marks() const { return m_vMarks; }

    // bow
    const Target& f_Target() const { return m_oTarget; }
    bool f_Aiming() const { return m_bAiming; }
    glm::vec2 f_AimFrom() const { return m_v2AimFrom; }
    glm::vec2 f_AimTo() const { return m_v2AimTo; }
    glm::vec2 f_Crosshair() const;                  // where the arrow would go now (with the shake)
    float f_Charge() const;                         // the pull's charge now, 0..1
    float f_ShotCharge() const { return m_iShots > 0 ? m_fChargeSum / (float)m_iShots : 0.0f; }   // the shots' average
    // slice
    const CueTrack& f_Cues() const { return m_oCueTrack; }
    const std::string& f_CueKind(int l_iK) const { return m_vCueKinds[(size_t)l_iK]; }   // "way" or a shape
    int f_CueDir(int l_iK) const { return m_vCueDirs[(size_t)l_iK]; }                      // a way: 0..7 (×45°, 0 = right, clockwise)
    glm::vec2 f_PromptAt() const;                   // where the prompt shows
    bool f_Swiping() const { return m_bAiming && m_eKind == PlayKind::Slice; }
    const std::vector<glm::vec2>& f_Stroke() const { return m_vStroke; }
    // circles
    const std::vector<Circle>& f_Circles() const { return m_vCircles; }
    CirclePhase f_CirclePhase() const { return m_eCirclePhase; }
    int f_CirclesLit() const;                       // the count: how many have shown their number so far
    // shape
    const ShapeTrace& f_Trace() const { return m_oTrace; }

private:
    void f_NextTarget();
    glm::vec2 f_TargetPos() const;
    void f_FinishOne(bool l_bOk, bool l_bPerfect, float l_fX, float l_fY);
    void f_Release();
    void f_JudgeStroke();
    PlayKind m_eKind = PlayKind::Track;
    TouchPlaySettings m_oS;
    glm::vec4 m_v4Area{0.0f};
    std::mt19937 m_oRng;
    bool m_bRunning = false, m_bStarted = false, m_bDone = false, m_bUltimate = false, m_bFailed = false;
    int m_iPoints = 0, m_iIndex = 0, m_iHitCount = 0, m_iPerfects = 0;
    int m_iClockMs = 0, m_iAgeMs = 0, m_iOneMs = 0, m_iLastAnswerMs = 0;   // since the start; since armed; the current one's; the last answer
    std::vector<Mark> m_vMarks;
    std::vector<Hit> m_vHits;
    // bow
    Target m_oTarget;
    glm::vec2 m_v2TargetFrom{0.0f}, m_v2TargetTo{0.0f};
    int m_iTargetAgeMs = 0, m_iHoldMs = 0, m_iShots = 0;
    float m_fChargeSum = 0.0f;
    bool m_bAiming = false;
    glm::vec2 m_v2AimFrom{0.0f}, m_v2AimTo{0.0f};
    // slice
    CueTrack m_oCueTrack;
    std::vector<std::string> m_vCueKinds;
    std::vector<int> m_vCueDirs;
    std::vector<glm::vec2> m_vStroke;
    // circles
    std::vector<Circle> m_vCircles;
    CirclePhase m_eCirclePhase = CirclePhase::Count;
    int m_iPhaseMs = 0;
    // shape
    ShapeTrace m_oTrace;
};

} // namespace toms

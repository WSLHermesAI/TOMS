// touch_play.h -- each weapon's own play, and the plays of moves and special guards (docs/gameplay/15 §3.3,
// docs/gameplay/16_SKILLS_AND_PLAYS.md §3, §6), built from the reusable blocks (cue_track.h, gesture.h):
//   Bow      the target moves a moment, then stops; touch anywhere, drag back and let go (a slingshot): the crosshair
//            goes the other way. Holding the pull charges the shot (up to a limit: then it goes) and shakes the aim.
//            A normal attack is one arrow; a move asks for several.
//   Swipe    the sword's prompts, one at a time on a cue track: each pops in, and only its input window takes the
//            answer: a swipe the arrow's way, a gesture (V, M, square; 17 §4.2) or a shape drawn. Each answer is a hit at once.
//   Circles  the staff's circles: they count in order with their numbers, all hide at once, then come back one by one
//            (0.1 s apart) with their numbers, already to be tapped; once all are back the numbers go, and the rest are
//            tapped in the order remembered. Each tap casts a spell at once (docs/gameplay/20 §2).
//   Recall   the memory blade (docs/gameplay/22 §2): the prompts show one by one (each showMs), all hide, then the
//            player swipes them back in order from memory, all in one answer time. A red prompt asks for the opposite way.
//            A wrong swipe misses that prompt and the next is due (as the staff's circles); time up: the rest are missed.
//   Chain    a skill's row of commands (22 §3): all shown on a bar at the top, the current one big; swiped in order (ways,
//            gestures or shapes) in one time. A wrong one misses it and the next is due; what was done counts.
//   Shape    a shape shown on the screen, traced corner by corner in a time (the staff's pentagram, a special guard).
//   Mash     tap quickly, `points` times in a time (a special guard).
//   Free     the ultimates (17 §5): for a time, any swipe or any tap anywhere counts, as many as the player makes.
// Every play reports on the track's scale (combo_play.h's ComboResult: hits of points, perfects, failed), so the
// result rows, their particles and clips, the break gauge and the grade work the same for all; takeHits() gives each
// answer as it happens, for the plays whose answers hit at once.
//
// Pure: positions in design pixels, time in ms, a seed for the random places; no Game dependency.
#pragma once
#include "game_names.h"    // toms::state, toms::battle_event, ... (no hard-coded name)
#include "field_keys.h"    // toms::field: every data field by its constant
#include "combo_play.h"
#include "cue_track.h"
#include "gesture.h"
#include "play_type.h"

#include <glm/glm.hpp>
#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace toms {

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
    std::vector<std::string> m_vSliceCues;    // each prompt: "way", a gesture ("v", "m", "square") or a shape ("triangle", "circle"); empty: the play's own
    std::map<std::string, int> m_mapCueExtraMs = {{toms::field::g_pszV, 400}, {toms::field::g_pszM, 900}, {toms::field::g_pszSquare, 1100}, {toms::field::g_pszTriangle, 800}, {toms::field::g_pszCircle, 800}};   // more time to draw it
    float m_fGestureExtraDeg = 0.0f;          // DEX: a gesture's angles are this much wider (Game)
    float m_fShapeTolerance = 0.2f;           // a drawn shape's corners, a share of its box (Game: wider with DEX)
    // recall (the memory blade; a weapon's "recall" rows override these, Game)
    int m_iRecallShowMs = 500;        // each prompt shown this long (the owner: 0.5 s)
    int m_iRecallGapMs = 80;          // a blank between two prompts, so the same way twice reads as two
    int m_iRecallReadyMs = 250;       // after the last, a "Go!" this long before the answers count
    int m_iRecallAnswerMs = 1000;     // the whole answer time (the owner: 1 s, adjustable)
    int m_iRecallPerCueMs = 0;        // ...plus this much per prompt
    float m_fRecallReverse = 0.0f;    // the share of prompts shown red: swipe the other way
    // chain (a skill's commands on the top bar)
    int m_iChainTimeMs = 5000;        // the whole chain's time (a move's timeMs sets it)
    // spin (a skill: circles round a ring; m_vSliceCues says each turn's way, "cw" or "ccw")
    int m_iSpinTimeMs = 6000;         // the whole play's time (a move's timeMs sets it; DEX adds)
    float m_fSpinRadius = 100.0f;     // the ring drawn (a move's size / 2)
    float m_fSpinMinRadius = 30.0f;   // nearer the middle than this, the finger's angle is not read
    float m_fSpinTurnDeg = 330.0f;    // this much of a turn, the right way, is one circle (a little short of 360: a hand's circle)
    int m_iSpinPerfectMs = 900;       // a circle this soon after the last is perfect
    // circles
    int m_iCircleCountMs = 450;       // each circle lit in turn with its number
    int m_iCircleHideMs = 500;        // then all hidden (the owner: 0.5 s)
    int m_iCircleWarnMs = 0;          // then, if more than 0, back together with an "!", not yet to be tapped (16 §3.3; 0 since 20 §2)
    int m_iCircleReturnMs = 100;      // then back one by one this far apart, numbered and already to be tapped (20 §2)
    int m_iCircleTapMs = 900;         // then the time to tap them all: this much per circle
    int m_iCirclePerfectMs = 500;     // a tap this soon after the last answer is perfect
    float m_fCircleRadius = 34.0f;
    // the staff's slide (the owner, 2026-11-27): touch a circle, then move to the next in order without lifting; reaching
    // the one due takes it as a tap. Passing this share of the radius from a wrong circle's middle is a wrong tap.
    bool m_bCircleSlide = true;
    float m_fCircleSlideWrong = 0.6f;
    std::vector<int> m_vCircleTimesMs;        // when each lights up as they are shown (ms from the first); empty: m_iCircleCountMs apart
    std::vector<glm::vec2> m_vCircleSymbol;   // the circles' places as a symbol, 0..1 in the area, in tap order (data/circle_symbols.json); empty: anywhere
    // shape
    std::string m_strShape = toms::gesture::g_pszPentagram;
    float m_fShapeSize = 220.0f;
    float m_fShapeCornerRadius = 44.0f;
    float m_fCornerBonus = 0.0f;      // DEX: the corners are this share bigger (Game)
    int m_iShapeTimeMs = 6000;
    int m_iShapeStepPerfectMs = 1200; // a corner this soon after the last is perfect
    // mash
    int m_iMashTimeMs = 2000;
    // free
    int m_iFreeTimeMs = 3000;
    float m_fFreeMinSwipe = 25.0f;    // a shorter drag is not a swipe
    // the player's stats (Game applies them): STR lengthens the bow's charge limit, DEX calms its shake
    int m_iBowHoldPerStr = 40;        // ms more per STR point
    float m_fBowShakePerDex = 0.05f;  // the shake / (1 + DEX x this)
    // DEX widens every play's tolerance (docs/gameplay/17 §4.4)
    float m_fDexWayDeg = 1.5f;        // a swipe's and a gesture's angle: degrees per point
    float m_fDexWayMaxDeg = 40.0f;    // a way's whole tolerance stops here (the eight ways are 45 degrees apart)
    float m_fDexGestureMaxDeg = 15.0f;// a gesture's extra stops here
    float m_fDexCornerPct = 1.5f;     // a shape's corners: percent per point
    int m_iDexWindowMs = 10;          // the answer window: ms per point
};

// data/battle.json "plays" (anything missing keeps its default above).
TouchPlaySettings f_TouchPlayFromJson(const nlohmann::json& l_jBattleJson);

class TouchPlay {
public:
    struct Target { float m_fX = 0, m_fY = 0, m_fR = 0; };
    struct Circle { float m_fX = 0, m_fY = 0; int m_iState = 0; };   // 0 wait 1 hit 2 perfect 3 missed
    struct Mark { float m_fX = 0, m_fY = 0; bool m_bOk = false; int m_iAgeMs = 0; };
    // One answer, as it happens: which one (0 = the first) and, for a swipe, the way it went (degrees, 0 = right, clockwise).
    struct Hit { bool m_bOk = false, m_bPerfect = false; float m_fX = 0, m_fY = 0; int m_iOrder = 0; float m_fAngle = 0; };
    enum class CirclePhase { Count, Hide, Warn, Tap };
    enum class RecallPhase { Show, Ready, Answer };   // recall: shown one by one, "Go!", the answers (chain: always Answer)

    // A new play of `points` over `area` (x, y, w, h: where its targets and circles may go). A move (`ultimate`) and
    // the sword, staff, shape and mash plays start at once; the bow waits for its first touch.
    void f_Start(PlayType l_eType, int l_iPoints, bool l_bUltimate, const glm::vec4& l_v4Area, uint32_t l_uSeed,
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
    PlayType f_Type() const { return m_eType; }
    int f_Hits() const { return m_iHitCount; }
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
    bool f_CueReversed(int l_iK) const { return l_iK >= 0 && l_iK < (int)m_vCueReversed.size() && m_vCueReversed[(size_t)l_iK]; }
    // recall and chain
    RecallPhase f_RecallPhase() const { return m_eRecallPhase; }
    int f_RecallShowing() const;                    // recall, Show: the prompt on the screen now (-1: none, between two)
    int f_RecallShownAgeMs() const;                 // ...since it popped in
    int f_AnswerTimeMs() const;                     // the whole answer time (recall) or the chain's time
    bool f_Swiping() const { return m_bAiming && (f_IsSwipePlay(m_eType) || m_eType == PlayType::eFREE_SWIPE || m_eType == PlayType::eFREE_SHOT || m_eType == PlayType::eSPIN_CIRCLE); }
    const std::vector<glm::vec2>& f_Stroke() const { return m_vStroke; }
    // circles
    const std::vector<Circle>& f_Circles() const { return m_vCircles; }
    CirclePhase f_CirclePhase() const { return m_eCirclePhase; }
    int f_CirclesLit() const;                       // the count: how many have shown their number so far
    int f_CirclesShown() const;                     // on the screen now: lit while counting, none hidden, then back one by one
    bool f_CircleNumbersShown() const;              // counting, or coming back (until the last is back)
    bool f_CircleSymbol() const { return m_bCircleSymbol; }     // laid out as a symbol (else anywhere, apart)
    // shape
    const ShapeTrace& f_Trace() const { return m_oTrace; }
    // spin
    glm::vec2 f_SpinCentre() const { return m_v2SpinC; }
    float f_SpinRadius() const { return m_oS.m_fSpinRadius; }
    bool f_SpinClockwise(int l_iK) const;           // the turn k's way (true: clockwise on the screen)
    float f_SpinProgress() const { return std::clamp(m_fSpinTurn / std::max(1.0f, m_oS.m_fSpinTurnDeg), 0.0f, 1.0f); }   // the turn going now, 0..1

private:
    void f_NextTarget();
    glm::vec2 f_TargetPos() const;
    void f_FinishOne(bool l_bOk, bool l_bPerfect, float l_fX, float l_fY);
    void f_Release();
    void f_JudgeStroke();
    void f_SpinPoint(glm::vec2 l_v2P);
    PlayType m_eType = PlayType::eTRACK;
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
    std::vector<char> m_vCueReversed;
    RecallPhase m_eRecallPhase = RecallPhase::Answer;
    int m_iRecallMs = 0;           // in the current recall phase
    std::vector<glm::vec2> m_vStroke;
    float m_fStrokeAngle = 0.0f;   // the last judged swipe's way (Hit::m_fAngle)
    // circles
    std::vector<Circle> m_vCircles;
    bool m_bCircleSymbol = false;    // this play's layout: a symbol
    bool m_bCircleSliding = false;   // the finger went down on a circle and has not lifted: moving on takes the next
    CirclePhase m_eCirclePhase = CirclePhase::Count;
    int m_iPhaseMs = 0;
    // spin
    glm::vec2 m_v2SpinC{0.0f};
    float m_fSpinTurn = 0.0f;      // degrees turned the right way in the current circle (never below 0)
    float m_fSpinLast = 0.0f;      // the last angle read
    bool m_bSpinHasLast = false;   // (none after a lift, or near the middle)
    // shape
    ShapeTrace m_oTrace;
};

} // namespace toms

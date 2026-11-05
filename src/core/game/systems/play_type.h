// play_type.h -- every way of playing an attack is one play type, named in data (docs/gameplay/17_WEAPONS_AND_PLAYS.md §4.1):
// weapon_play.json "play", special_moves.json "play": { "kind": ... }, a scenario's battle "play".
// The types still to come (rhythmTap, holdCharge, tapTarget, reload, volleySet) join this list with their plays.
//
// Pure: no Game dependency.
#pragma once
#include <string>

namespace toms {

enum class PlayType {
    eTRACK,            // "track"          the point track (a scenario's default; a weapon with no play of its own)
    eSWIPE_WAY,        // "swipeWay"       swipe one of 8 ways; the prompts may also ask for gestures
    eSWIPE_GESTURE,    // "swipeGesture"   a gesture (V, M, square), any direction
    eDRAW_SHAPE,       // "drawShape"      trace a shown shape, point by point
    eSEQUENCE_CLICK,   // "sequenceClick"  tap the shown spots in order
    ePULL_RELEASE,     // "pullRelease"    drag back and let go at a target
    eMASH,             // "mash"           tap as fast as possible
    eFREE_SWIPE,       // "freeSwipe"      any swipe anywhere counts, for a time
    eFREE_TAP,         // "freeTap"        any tap anywhere counts, for a time
    eFREE_SHOT,        // "freeShot"       any swipe shoots and always hits, for a time
};

// A data name; the older names still work (a saved battle setup may use them). Anything else: the track.
inline PlayType f_PlayTypeFromString(const std::string& l_strS) {
    if (l_strS == "swipeWay" || l_strS == "slice") return PlayType::eSWIPE_WAY;
    if (l_strS == "swipeGesture") return PlayType::eSWIPE_GESTURE;
    if (l_strS == "drawShape" || l_strS == "shape") return PlayType::eDRAW_SHAPE;
    if (l_strS == "sequenceClick" || l_strS == "circles") return PlayType::eSEQUENCE_CLICK;
    if (l_strS == "pullRelease" || l_strS == "bow") return PlayType::ePULL_RELEASE;
    if (l_strS == "mash") return PlayType::eMASH;
    if (l_strS == "freeSwipe") return PlayType::eFREE_SWIPE;
    if (l_strS == "freeTap") return PlayType::eFREE_TAP;
    if (l_strS == "freeShot") return PlayType::eFREE_SHOT;
    return PlayType::eTRACK;
}

inline const char* f_PlayTypeName(PlayType l_eT) {
    switch (l_eT) {
    case PlayType::eSWIPE_WAY: return "swipeWay";
    case PlayType::eSWIPE_GESTURE: return "swipeGesture";
    case PlayType::eDRAW_SHAPE: return "drawShape";
    case PlayType::eSEQUENCE_CLICK: return "sequenceClick";
    case PlayType::ePULL_RELEASE: return "pullRelease";
    case PlayType::eMASH: return "mash";
    case PlayType::eFREE_SWIPE: return "freeSwipe";
    case PlayType::eFREE_TAP: return "freeTap";
    case PlayType::eFREE_SHOT: return "freeShot";
    default: return "track";
    }
}

inline bool f_IsSwipePlay(PlayType l_eT) { return l_eT == PlayType::eSWIPE_WAY || l_eT == PlayType::eSWIPE_GESTURE; }
inline bool f_IsFreePlay(PlayType l_eT) { return l_eT == PlayType::eFREE_SWIPE || l_eT == PlayType::eFREE_TAP || l_eT == PlayType::eFREE_SHOT; }

} // namespace toms

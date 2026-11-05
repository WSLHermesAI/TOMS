// special_moves.h -- special moves as data (docs/gameplay/16_SKILLS_AND_PLAYS.md §2 blocks B4 and B5, §6): data/special_moves.json.
//
// A skill is a name, a play, three shows and its damage. The same entry kind serves a weapon's ultimate, a skill from
// the skill list, and a monster's special guard play (moves.json "guardPlay"); a new move is a new entry:
//
//   "star_seal": {
//     "name": "skill.star_seal", "weapon": "staff", "use": "ultimate",
//     "play": { "kind": "shape", "shape": "pentagram", "timeMs": 6000, "size": 230, "cornerRadius": 44 },
//     "demo":   [ { "at": 0, "do": "pathFx", "shape": "pentagram", "fx": "star_trail", "ms": 2000, "corner": "star_burst" },
//                 { "at": 2100, "do": "dim" }, { "at": 2100, "do": "countdown" } ],
//     "step":   [ { "do": "fx", "fx": "star_burst_big" }, { "do": "count" }, { "do": "namePart" } ],
//     "finish": [ { "do": "name" }, { "at": 0, "do": "camera", "zoom": 1.5, "on": "player", "ms": 1600 },
//                 { "at": 200, "do": "clip", "clip": "attack_big" }, { "at": 400, "do": "hits", "count": 10, "gapMs": 110 } ],
//     "damage": 10
//   }
//
// play.kind: a play type's data name (play_type.h): drawShape (trace a shown shape), swipeWay (the sword's prompts: "cues"
// ["way", "way", "v"]), pullRelease (aim the moving targets), sequenceClick (the staff's circles), mash (tap `points` times),
// freeSwipe / freeTap / freeShot (any swipe or tap counts for timeMs, each a hit of hitDamage times a normal attack).
// A show is a list of beats, each at a time
// ("at", ms from the show's start; "step" beats at each step): pathFx (particles along a shape, and a "corner" effect
// at each corner), fx (an effect: at the step, the player or the monster), dim, countdown, banner (a text key), name /
// namePart (the move's name, whole or a step's share), count ("3/5"), camera (a close-up), clip (the player's clip),
// hits (the move's hits, its damage shared over them), shake, sound.
//
// Pure: data and functions only; no Game dependency.
#pragma once
#include <json.hpp>

#include "play_type.h"

#include <map>
#include <string>
#include <vector>

namespace toms {

struct ShowBeat {
    int m_iAtMs = 0;
    std::string m_strWhat;               // "do": pathFx fx dim countdown banner name namePart count camera clip hits shake sound
    std::string m_strFx, m_strCorner, m_strShape, m_strClip, m_strText, m_strOn;   // its effect, a pathFx's corner effect, the shape, the clip, a text key, who
    int m_iMs = 0;                     // how long (pathFx, camera, dim)
    int m_iCount = 0, m_iGapMs = 0;       // hits
    float m_fValue = 0.0f;             // dim strength, shake strength
    float m_fZoom = 1.0f;              // camera
};

struct SkillPlaySpec {
    PlayType m_eType = PlayType::eDRAW_SHAPE;
    std::string m_strShape = "pentagram";
    int m_iPoints = 5;                 // shape: its corners (set from the shape); others: how many
    int m_iTimeMs = 6000;              // shape / mash / free: the whole play's time
    float m_fHitDamage = 1.0f;         // free: each swipe or tap, times a normal attack
    float m_fSize = 220.0f;            // shape: on the screen
    float m_fCornerRadius = 44.0f;
    std::vector<std::string> m_vCues;  // swipe: each prompt, "way", a gesture or a shape name
};

struct SpecialMove {
    std::string m_strId, m_strName, m_strWeapon, m_strUse = "ultimate";   // use: ultimate, skill, guard
    SkillPlaySpec m_oPlay;
    std::vector<ShowBeat> m_vDemo, m_vStep, m_vFinish;
    float m_fDamage = 10.0f;           // times a normal attack (the owner: N = 10 to start); a guard skill: unused
    std::string m_strPrompt;             // a text key over the play ("Draw the pentagram!")
};

struct SpecialMoveTable {
    std::map<std::string, SpecialMove> m_mapSkills;
    const SpecialMove* f_Find(const std::string& l_strId) const;
    const SpecialMove* f_ForWeapon(const std::string& l_strWeapon, const std::string& l_strUse) const;   // the first of that use
};

SpecialMoveTable f_SpecialMovesFromJson(const nlohmann::json& l_jJ);   // keys starting with "_" are comments

// A show's end: its last beat's time plus its length (a hits beat: count x gapMs).
int f_ShowLength(const std::vector<ShowBeat>& l_vBeats);
// The beats whose time is in (fromMs, toMs] (fromMs < 0: from the start, the beats at 0 too).
std::vector<const ShowBeat*> f_BeatsDue(const std::vector<ShowBeat>& l_vBeats, int l_iFromMs, int l_iToMs);
// The move's name as far as a step: the first `done` of `total` shares of its letters (UTF-8 kept whole).
std::string f_NamePart(const std::string& l_strName, int l_iDone, int l_iTotal);

} // namespace toms

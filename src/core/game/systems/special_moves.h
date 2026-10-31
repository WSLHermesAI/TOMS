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
// play.kind: shape (trace a shown shape), slice (the sword's prompts: "cues" ["way", "way", "triangle"]), bow (aim the
// moving targets), circles (the staff's circles), mash (tap `points` times). A show is a list of beats, each at a time
// ("at", ms from the show's start; "step" beats at each step): pathFx (particles along a shape, and a "corner" effect
// at each corner), fx (an effect: at the step, the player or the monster), dim, countdown, banner (a text key), name /
// namePart (the move's name, whole or a step's share), count ("3/5"), camera (a close-up), clip (the player's clip),
// hits (the move's hits, its damage shared over them), shake, sound.
//
// Pure: data and functions only; no Game dependency.
#pragma once
#include <json.hpp>

#include <map>
#include <string>
#include <vector>

namespace toms {

struct ShowBeat {
    int atMs = 0;
    std::string what;               // "do": pathFx fx dim countdown banner name namePart count camera clip hits shake sound
    std::string fx, corner, shape, clip, text, on;   // its effect, a pathFx's corner effect, the shape, the clip, a text key, who
    int ms = 0;                     // how long (pathFx, camera, dim)
    int count = 0, gapMs = 0;       // hits
    float value = 0.0f;             // dim strength, shake strength
    float zoom = 1.0f;              // camera
};

struct SkillPlaySpec {
    std::string kind = "shape";     // shape slice bow circles mash
    std::string shape = "pentagram";
    int points = 5;                 // shape: its corners (set from the shape); others: how many
    int timeMs = 6000;              // shape / mash: the whole play's time
    float size = 220.0f;            // shape: on the screen
    float cornerRadius = 44.0f;
    std::vector<std::string> cues;  // slice: each prompt, "way" or a shape name
};

struct SpecialMove {
    std::string id, name, weapon, use = "ultimate";   // use: ultimate, skill, guard
    SkillPlaySpec play;
    std::vector<ShowBeat> demo, step, finish;
    float damage = 10.0f;           // times a normal attack (the owner: N = 10 to start); a guard skill: unused
    std::string prompt;             // a text key over the play ("Draw the pentagram!")
};

struct SpecialMoveTable {
    std::map<std::string, SpecialMove> skills;
    const SpecialMove* find(const std::string& id) const;
    const SpecialMove* forWeapon(const std::string& weapon, const std::string& use) const;   // the first of that use
};

SpecialMoveTable specialMovesFromJson(const nlohmann::json& j);   // keys starting with "_" are comments

// A show's end: its last beat's time plus its length (a hits beat: count x gapMs).
int showLength(const std::vector<ShowBeat>& beats);
// The beats whose time is in (fromMs, toMs] (fromMs < 0: from the start, the beats at 0 too).
std::vector<const ShowBeat*> beatsDue(const std::vector<ShowBeat>& beats, int fromMs, int toMs);
// The move's name as far as a step: the first `done` of `total` shares of its letters (UTF-8 kept whole).
std::string namePart(const std::string& name, int done, int total);

} // namespace toms

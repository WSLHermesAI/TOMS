// card_collection.h -- monster cards: a collection (docs/gameplay/07_GAMEPLAY_PLAN.md §3, the owner's decision 2026-10-20).
//
// For now cards are only a collection. Per monster the player has a record: how many times they defeated it, how
// many times it defeated them, and how many copies of its card they hold. Defeating a monster may give its card:
// the first copy and further copies have their own chance, set per monster in data/enemies.json:
//
//   "slime": { ..., "card": { "chance": 1.0, "repeatChance": 0.5 } }      (missing: 1.0 and 0.5)
//
// The records live in the meta save, so they survive runs and rebirths. Merging cards is a later idea (§3.1).
// Pure data + pure functions, no Game dependency (the same shape as skill_system.h / weapon_play.h).
#pragma once
#include <json.hpp>
#include <map>
#include <string>

namespace toms {

struct CardRecord {
    int copies = 0;        // cards of this monster held
    int defeated = 0;      // times the player defeated it
    int defeatedBy = 0;    // times it defeated the player
};
using CardBook = std::map<std::string, CardRecord>;   // monster id -> its record

struct CardChance {
    float first = 1.0f;    // "chance": the card when the player has none yet
    float repeat = 0.5f;   // "repeatChance": another copy
};

// The monster's "card" settings from its enemies.json entry (defaults for anything missing; clamped to 0..1).
CardChance cardChanceFromJson(const nlohmann::json& enemyEntry);

// A win against `monster`: counts it, then gives a card when roll01 (a random number in [0, 1)) is under the chance
// for the copies held. Returns true when a card was given.
bool recordCardWin(CardBook& book, const std::string& monster, const CardChance& chance, float roll01);
// A loss against `monster`: counted.
void recordCardLoss(CardBook& book, const std::string& monster);

nlohmann::json cardBookToJson(const CardBook& book);
// Unknown or broken entries are skipped; negative numbers read as 0.
CardBook cardBookFromJson(const nlohmann::json& j);

} // namespace toms

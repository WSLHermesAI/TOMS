// battle_rules_test.cpp -- looks, monster events, moves, the pace and the break gauge (battle_rules.h). Exits 0 on success.
#include "battle_rules.h"

#include <cstdio>

using namespace toms;
using nlohmann::json;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); g_fail++; } } while (0)

int main() {
    // 1. Looks: a state from the look, from its base, and the fallback look for one that is not there.
    const LooksTable looks = looksFromJson(json::parse(R"({
        "_comment": "x",
        "monster": { "states": { "attack": { "sound": "enemy_attack" }, "die": { "sound": "enemy_down", "fx": "puff" } } },
        "golem":   { "base": "monster", "states": { "attack": { "clip": "slam", "sound": "golem_slam", "fx": "dust" } } }
    })"));
    const LookState* a = looks.find("golem", "attack");
    CHECK(a && a->clip == "slam" && a->sound == "golem_slam" && a->fx == "dust", "the look's own state");
    const LookState* d = looks.find("golem", "die");
    CHECK(d && d->sound == "enemy_down" && d->fx == "puff", "a state from the base look");
    const LookState* f = looks.find("nobody", "attack");
    CHECK(f && f->sound == "enemy_attack", "an unknown look falls back to \"monster\"");
    CHECK(looks.find("golem", "idle") == nullptr, "no state in the chain: null");

    // 2. Monster events.
    std::vector<MonsterEvent> ev = monsterEventsFromJson(json::parse(R"([
        { "on": "hpBelow", "value": 50, "do": [ { "say": "Runes blaze" }, { "stat": "atk", "mul": 1.3 } ] },
        { "on": "hpBelow", "value": 20, "do": [ { "transformInto": "golem_core", "hp": "keepShare" } ] },
        { "on": "attacks", "value": 2, "once": false, "do": [ { "say": "again" } ] },
        { "on": "everyMs", "value": 1000, "do": [ { "heal": 5 } ] },
        { "on": "battleStart", "do": [] }
    ])"));
    CHECK(ev.size() == 5 && ev[0].once && !ev[2].once && !ev[3].once, "events parsed, everyMs repeats by default");
    EventCheck hit; hit.on = "hpBelow"; hit.hpPctBefore = 80; hit.hpPctAfter = 60;
    CHECK(fireMonsterEvents(ev, hit).empty(), "above 50 %: nothing");
    hit.hpPctBefore = 60; hit.hpPctAfter = 10;
    std::vector<int> fired = fireMonsterEvents(ev, hit);
    CHECK(fired.size() == 2 && fired[0] == 0 && fired[1] == 1, "one hit crossing 50 and 20: both fire");
    CHECK(fireMonsterEvents(ev, hit).empty(), "once: they do not fire again");
    EventCheck at; at.on = "attacks";
    at.attacks = 1; CHECK(fireMonsterEvents(ev, at).empty(), "attack 1 of every 2: no");
    at.attacks = 2; CHECK(fireMonsterEvents(ev, at).size() == 1, "attack 2: yes");
    at.attacks = 4; CHECK(fireMonsterEvents(ev, at).size() == 1, "attack 4: again (once: false)");
    EventCheck t; t.on = "everyMs"; t.msBefore = 900; t.msAfter = 1016;
    CHECK(fireMonsterEvents(ev, t).size() == 1, "every 1000 ms: crossing 1000");
    t.msBefore = 1016; t.msAfter = 1032;
    CHECK(fireMonsterEvents(ev, t).empty(), "not again until 2000");
    EventCheck s; s.on = "battleStart";
    CHECK(fireMonsterEvents(ev, s).size() == 1 && fireMonsterEvents(ev, s).empty(), "battleStart once");

    // 3. Moves and the choice of the next one.
    const MovesTable moves = movesFromJson(json::parse(R"({
        "heavy": { "kind": "heavy", "damage": 1.8, "time": 1.3 },
        "jabs":  { "kind": "jabs", "damage": 0.45, "hits": 3 },
        "ward":  { "kind": "guard", "reduce": 0.25 }
    })"));
    CHECK(moves.get("heavy").kind == MoveKind::Heavy && moves.get("heavy").damage > 1.7f && moves.get("heavy").time > 1.2f,
          "heavy parsed");
    CHECK(moves.get("jabs").hits == 3 && moves.get("ward").kind == MoveKind::Guard && moves.get("ward").reduce < 0.3f,
          "jabs and a guard by another name");
    CHECK(moves.get("heavy").text == "battle.intent.heavy", "the intent's words default to battle.intent.<kind>");
    CHECK(moves.get("nothing").kind == MoveKind::Strike, "an unknown move: a strike");
    const std::vector<MoveChoice> ch = moveChoicesFromJson(json::parse(R"(["strike", { "id": "heavy", "weight": 3 }])"));
    CHECK(ch.size() == 2 && ch[1].weight == 3, "choices parsed");
    CHECK(pickMove(ch, 0.0f) == "strike" && pickMove(ch, 0.24f) == "strike" && pickMove(ch, 0.25f) == "heavy" &&
              pickMove(ch, 0.999f) == "heavy", "the weights split the roll");
    CHECK(pickMove({}, 0.5f) == "strike", "no moves: a strike");

    // 4. The pace.
    const PaceSettings p = paceFromJson(json::parse(R"({ "pace": "stamina", "stamina": { "max": 80, "attack": 30 } })"));
    CHECK(p.pace == Pace::Stamina && p.staminaMax == 80 && p.attackCost == 30 && p.guardCost == 20, "stamina pace");
    CHECK(paceFromJson(json::object()).pace == Pace::Cooldown, "no pace: cooldowns (as before)");
    CHECK(paceFromString("bogus", Pace::Stamina) == Pace::Stamina && paceFromString("cooldown", Pace::Stamina) == Pace::Cooldown,
          "an override, or the fallback");

    // 5. Break.
    const BreakSettings b = breakFromJson(json::parse(R"({ "break": { "points": 5, "parryPower": 90 } })"));
    CHECK(b.points == 5 && b.parryPower == 90 && b.brokenMs == 3000, "break settings");
    float g = 0.0f;
    CHECK(!addBreak(g, 3, 5) && g == 3.0f, "3 of 5");
    CHECK(addBreak(g, 2, 5) && g == 0.0f, "5 of 5: broken, the gauge empties");

    if (g_fail) { std::printf("battle_rules_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("battle_rules_test: all passed\n");
    return 0;
}

// game_scenario.cpp -- test scenarios: start the game straight in one scene from a setting file
// (docs/31_TEST_SCENARIOS.md). toms_game --scenario=<folder or file> plays it; --scenario-test also runs the
// scenario's own "test" frames and keys and exits 3 when scenarioCheck fails (tests/scenarios/, ctest scenario.*).
//
// A scenario never writes a save: its run has no slot (newGame(-1)), and a save it starts from is read from the
// scenario's folder, never from the player's save folder.
#include "game.h"
#include "game_internal.h"

using namespace toms::game_detail;   // trParam

#include <filesystem>

namespace {

namespace fs = std::filesystem;

// "include" files first (each may include others), then this file's own fields on top: objects merge key by key,
// anything else is replaced. Paths are relative to the file that names them.
bool readScenarioJson(const fs::path& file, nlohmann::json& out, std::string& error, int depth = 0) {
    if (depth > 8) { error = "include nested too deep at " + file.string(); return false; }
    nlohmann::json j = toms::readJsonFileSafe(file.string());
    if (!j.is_object()) { error = "cannot read " + file.string() + " (missing, or not a JSON object)"; return false; }
    // file names in this file are relative to it: made absolute here, so an included file's save or dialogue is
    // found next to that file, not next to the scenario that includes it
    auto absolute = [&](nlohmann::json& v) { if (v.is_string()) v = (file.parent_path() / v.get<std::string>()).string(); };
    if (j.contains("save")) absolute(j["save"]);
    if (j.contains("dialogue") && j["dialogue"].is_object() && j["dialogue"].contains("file")) absolute(j["dialogue"]["file"]);
    if (j.contains("stage") && j["stage"].is_string()) {
        const std::string st = j["stage"].get<std::string>();
        if (st.size() > 5 && st.compare(st.size() - 5, 5, ".json") == 0) absolute(j["stage"]);
    }
    nlohmann::json base = nlohmann::json::object();
    if (j.contains("include")) {
        const nlohmann::json inc = j["include"].is_array() ? j["include"] : nlohmann::json::array({j["include"]});
        for (const auto& name : inc) {
            if (!name.is_string()) continue;
            nlohmann::json part;
            if (!readScenarioJson(file.parent_path() / name.get<std::string>(), part, error, depth + 1)) return false;
            base.merge_patch(part);
        }
        j.erase("include");
    }
    base.merge_patch(j);
    out = std::move(base);
    return true;
}

std::vector<std::string> strings(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.contains(key)) return out;
    const nlohmann::json& v = j[key];
    if (v.is_string()) out.push_back(v.get<std::string>());
    else if (v.is_array()) for (const auto& s : v) if (s.is_string()) out.push_back(s.get<std::string>());
    return out;
}

int menuTabIndex(const std::string& name) {
    static const char* tabs[] = {"status", "gear", "items", "events", "cards", "system"};   // Game::MenuTab order
    for (int i = 0; i < 6; ++i) if (name == tabs[i]) return i;
    return -1;
}

} // namespace

bool Game::startScenario(const std::string& pathIn, std::string& error) {
    fs::path file = pathIn;
    std::error_code ec;
    if (fs::is_directory(file, ec)) file /= "scenario.json";
    nlohmann::json sc;
    if (!readScenarioJson(file, sc, error)) return false;
    return startScenarioJson(sc, file.parent_path().filename().string(), error);
}

bool Game::enemyFromData(const std::string& id, const nlohmann::json& b, EnemyInst& en) const {
    auto t = enemyTpl.find(id);
    if (t == enemyTpl.end()) return false;
    en = EnemyInst{};
    en.id = id; en.name = locale_.field(t->second["name"]);
    en.hp = b.value("hp", t->second.value("hp", 1)); en.atk = b.value("atk", t->second.value("atk", 0));
    en.def = b.value("def", t->second.value("def", 0));
    en.exp = t->second.value("exp", 0); en.gold = t->second.value("gold", 0);
    en.x = en.y = -1;   // not on the map: winning removes nothing
    en.boss = t->second.value("boss", false);
    en.atkIntervalMs = b.value("atk_interval_ms", t->second.value("atk_interval_ms", 4000));
    return true;
}

void Game::arenaNextFight() {
    if (arena_.enemies.empty()) { arena_.on = false; return; }
    EnemyInst en;
    const std::string id = arena_.enemies[arena_.next % arena_.enemies.size()];
    arena_.next++;
    if (!enemyFromData(id, arena_.overrides, en)) { arena_.on = false; return; }
    pl.hp = effectiveMaxHp();   // every fight starts at full health
    arena_.fights++;
    startCombat(en);
    cs.log = trParam(trParam(trParam(locale_.tr("arena.summary"), "n", std::to_string(arena_.fights)),
                             "w", std::to_string(arena_.wins)), "l", std::to_string(arena_.losses));
}

bool Game::arenaEndFight(bool won) {
    if (!arena_.on) return false;
    cs.active = false;
    cs.won = won;   // a win waits for the player's tap; a loss shows briefly
    if (won) { arena_.wins++; cs.log = locale_.tr("arena.won"); }
    else { arena_.losses++; cs.log = locale_.tr("arena.lost"); cs.resultPauseMs = 1200; }
    return true;
}

bool Game::startScenarioJson(const nlohmann::json& sc, const std::string& defaultName, std::string& error) {
    scenarioName_ = sc.value("name", defaultName);
    arena_ = Arena{};
    scenarioScene_ = sc.value("scene", std::string("stage"));
    scenarioExpect_ = sc.value("expect", nlohmann::json::object());
    const nlohmann::json test = sc.value("test", nlohmann::json::object());
    scenarioTestFrames_ = test.value("frames", 0);
    scenarioTestKeys_ = test.value("keys", std::string());
    auto bad = [&](const std::string& what) { error = scenarioName_ + ": " + what; return false; };

    // 1. The run: from a save file in the scenario's folder, or a new game. Neither is ever written.
    if (scenarioScene_ == "title") return true;   // the title as the game starts: nothing to set up
    if (sc.contains("save")) {
        toms::MetaSaveData m;
        toms::RunSaveData r;
        int playSec = 0;
        const fs::path save = sc["save"].get<std::string>();   // absolute (readScenarioJson)
        if (!toms::readSaveFile(save.string(), m, r, &playSec)) return bad("cannot read the save " + save.string());
        applyLoadedRun(m, r, /*slot=*/0, playSec);
        slotReadOnly_ = true;
    } else {
        newGame(-1);
    }

    // 2. The floor: a floor id (F04), a stage id, or a stage file next to the scenario.
    if (sc.contains("stage")) {
        loadStage(sc["stage"].get<std::string>());   // a stage file's path is already absolute (readScenarioJson)
    }

    // 3. The story so far: stair locks, finished events, flags.
    if (sc.contains("stairLocks"))
        for (const auto& l : sc["stairLocks"]) {   // "event": one, or "events": several (W38: all must be done)
            for (const std::string& e : strings(l, "event")) debugLockStairs(l.value("floor", std::string()), e);
            for (const std::string& e : strings(l, "events")) debugLockStairs(l.value("floor", std::string()), e);
        }
    for (const std::string& f : strings(sc, "flags")) debugSetFlag(f);
    for (const std::string& e : strings(sc, "eventsDone")) debugFinishEvent(e);

    // 4. The player: items and gear first (gear can change the stats), then the numbers as given.
    const nlohmann::json p = sc.value("player", nlohmann::json::object());
    for (const std::string& id : strings(p, "give")) debugGive(id);
    for (const std::string& id : strings(p, "equip"))
        if (!debugEquip(id)) return bad("no equipment \"" + id + "\" in data/items.json");
    if (p.contains("x")) pl.x = p["x"].get<int>();
    if (p.contains("y")) pl.y = p["y"].get<int>();
    if (p.contains("maxhp")) pl.maxhp = p["maxhp"].get<int>();
    if (p.contains("hp")) pl.hp = p["hp"].get<int>();
    if (p.contains("atk")) pl.atk = p["atk"].get<int>();
    if (p.contains("def")) pl.def = p["def"].get<int>();
    if (p.contains("gold")) pl.gold = p["gold"].get<int>();
    if (p.contains("exp")) pl.exp = p["exp"].get<int>();
    if (p.contains("lv")) pl.lv = p["lv"].get<int>();
    if (p.contains("keys")) {
        const nlohmann::json& k = p["keys"];
        pl.key_yellow = k.value("yellow", pl.key_yellow);
        pl.key_blue = k.value("blue", pl.key_blue);
        pl.key_red = k.value("red", pl.key_red);
    }
    if (p.contains("attrs"))
        for (auto it = p["attrs"].begin(); it != p["attrs"].end(); ++it) pl.attrs[it.key()] = it.value().get<int>();
    if (p.contains("inventory")) pl.inv = strings(p, "inventory");

    // 5. The scene.
    const std::string& s = scenarioScene_;
    if (s == "stage") {
        // the walking scene as it is now
    } else if (s == "battle" || s == "battleWon") {
        const nlohmann::json b = sc.value("battle", nlohmann::json::object());
        const std::string id = b.value("enemy", std::string());
        EnemyInst en;
        if (!enemyFromData(id, b, en)) return bad("no enemy \"" + id + "\" in data/enemies.json");
        startCombat(en);
        if (s == "battleWon") finishCombatWin();
    } else if (s == "arena") {
        // the weapon test arena: "battle": { "enemies": [...] } (or "enemy"), fought in turn; the weapon is the
        // player's ("player": { "equip": [...] }), unarmed without one
        const nlohmann::json b = sc.value("battle", nlohmann::json::object());
        arena_.enemies = strings(b, "enemies");
        if (arena_.enemies.empty()) arena_.enemies = strings(b, "enemy");
        if (arena_.enemies.empty()) return bad("\"battle\": { \"enemies\": [...] } is missing");
        for (const std::string& id : arena_.enemies) {
            EnemyInst probe;
            if (!enemyFromData(id, b, probe)) return bad("no enemy \"" + id + "\" in data/enemies.json");
        }
        arena_.overrides = b;
        arena_.on = true;
        arenaNextFight();
    } else if (s == "dialogue") {
        const nlohmann::json d = sc.value("dialogue", nlohmann::json::object());
        if (d.contains("file")) {
            const fs::path f = d["file"].get<std::string>();   // absolute (readScenarioJson)
            nlohmann::json data = toms::readJsonFileSafe(f.string());
            if (!data.is_object()) return bad("cannot read the dialogue " + f.string());
            dlgNpc = d.value("id", f.stem().string());
            dlgSpeaker_.clear(); dlgTalkNpc_.clear();
            openDialogue(data, DlgMode::Plain);
        } else {
            const std::string id = d.value("id", std::string());
            if (!readDialogue(id).is_object()) return bad("no dialogue \"" + id + "\" in data/dialogue/");
            startDialogue(id);
        }
        if (d.contains("node")) enterNode(d["node"].get<std::string>());
    } else if (s == "talk") {
        const std::string npc = sc.value("talk", nlohmann::json::object()).value("npc", std::string());
        if (npc.empty()) return bad("\"talk\": { \"npc\": ... } is missing");
        talkTo(npc);
    } else if (s == "store") {
        storeUnlocked_ = true;
        storeUnlockDlg = false;
        openStore();
    } else if (s == "menu") {
        const std::string tab = sc.value("menu", nlohmann::json::object()).value("tab", std::string("status"));
        const int i = menuTabIndex(tab);
        if (i < 0) return bad("menu tab \"" + tab + "\" (status, gear, items, events, cards, system)");
        openPlayerMenu(i);
    } else if (s == "saveLoad") {
        const std::string mode = sc.value("saveLoad", nlohmann::json::object()).value("mode", std::string("load"));
        openPlayerMenu((int)MenuTab::System);
        openSaveLoad(mode == "save" ? 0 : 1);
    } else if (s == "stageSelect") {
        openStageSelect();
    } else if (s == "stairsConfirm") {
        const nlohmann::json c = sc.value("stairs", nlohmann::json::object());
        requestStageTransition(c.value("to", std::string("F02")), c.value("up", true));
    } else if (s == "notice") {
        // the notice comes from the setup (a stair lock and its event finished); nothing more to open
    } else if (s == "ending") {
        const std::string id = sc.value("ending", nlohmann::json::object()).value("id", std::string());
        if (id.empty()) return bad("\"ending\": { \"id\": ... } is missing");
        triggerEnding(id);
    } else if (s == "hub") {
        const std::string loc = sc.value("hub", nlohmann::json::object()).value("location", std::string());
        if (!activateHubLocation(loc)) return bad("hub location \"" + loc + "\" is unknown or locked");
    } else {
        return bad("unknown scene \"" + s + "\" (docs/31_TEST_SCENARIOS.md lists them)");
    }
    std::fprintf(stderr, "[scenario] %s: scene %s started\n", scenarioName_.c_str(), s.c_str());
    return true;
}

bool Game::scenarioCheck(std::string& report) const {
    const std::string scene = scenarioExpect_.value("scene", scenarioScene_);
    bool ok = true;
    std::string out;
    auto fail = [&](const std::string& why) { ok = false; out += "\n  " + why; };
    bool showing = false;
    if (scene == "title") showing = title_.isOpen();
    else if (scene == "stage") showing = !title_.isOpen() && !modalActive();
    else if (scene == "battle") showing = cs.active;
    else if (scene == "battleWon") showing = cs.won;
    else if (scene == "arena") showing = arena_.on && (cs.active || cs.won || cs.resultPauseMs > 0);
    else if (scene == "dialogue" || scene == "talk") showing = inDialogue;
    else if (scene == "store") showing = storeOpen;
    else if (scene == "menu") showing = inGameMenuOpen_;
    else if (scene == "saveLoad") showing = saveLoadOpen_;
    else if (scene == "stageSelect") showing = stageSelectOpen_;
    else if (scene == "stairsConfirm") showing = stairsConfirmOpen_;
    else if (scene == "notice") showing = !notices_.empty();
    else if (scene == "ending") showing = endingActive();
    else if (scene == "hub") showing = inDialogue;
    if (!showing) fail("the scene \"" + scene + "\" is not showing");
    if (scenarioExpect_.contains("menuTab")) {
        const int want = menuTabIndex(scenarioExpect_["menuTab"].get<std::string>());
        if ((int)menuTab_ != want) fail("the menu is not on tab " + scenarioExpect_["menuTab"].get<std::string>());
    }
    auto num = [&](const char* key, int have) {
        if (scenarioExpect_.contains(key) && scenarioExpect_[key].get<int>() != have)
            fail(std::string(key) + " is " + std::to_string(have) + ", expected " + std::to_string(scenarioExpect_[key].get<int>()));
    };
    num("gold", pl.gold); num("hp", pl.hp); num("lv", pl.lv);
    num("arenaFights", arena_.fights); num("arenaWins", arena_.wins);
    if (scenarioExpect_.contains("stage") && curStage != scenarioExpect_["stage"].get<std::string>())
        fail("the floor is " + curStage + ", expected " + scenarioExpect_["stage"].get<std::string>());
    for (const std::string& id : strings(scenarioExpect_, "inventoryHas"))
        if (std::find(pl.inv.begin(), pl.inv.end(), id) == pl.inv.end()) fail("the inventory has no " + id);
    for (const std::string& f : strings(scenarioExpect_, "flags"))
        if (!run_.flag(f)) fail("the flag " + f + " is not set");
    if (scenarioExpect_.contains("cards") && scenarioExpect_["cards"].is_object())   // monster id -> copies held
        for (auto it = scenarioExpect_["cards"].begin(); it != scenarioExpect_["cards"].end(); ++it) {
            auto r = meta_.cards.find(it.key());
            const int have = r == meta_.cards.end() ? 0 : r->second.copies;
            if (have != it.value().get<int>())
                fail("cards of " + it.key() + ": " + std::to_string(have) + ", expected " + std::to_string(it.value().get<int>()));
        }
    if (scenarioExpect_.contains("stairUpLocked")) {   // the current floor's first stair up (W38 tests)
        const toms::StairLink* up = nullptr;
        for (const toms::StairLink& l : st.stairs) if (l.up) { up = &l; break; }
        const bool want = scenarioExpect_["stairUpLocked"].get<bool>();
        if (!up) fail("the floor has no stair up");
        else if (stairLocked(*up) != want) fail(std::string("the stair up is ") + (want ? "open, expected locked" : "locked, expected open"));
        if (up && scenarioExpect_.contains("stairUpMarked") &&
            stairJustOpened(st.id, *up) != scenarioExpect_["stairUpMarked"].get<bool>())
            fail(std::string("the stair up is ") + (scenarioExpect_["stairUpMarked"].get<bool>() ? "not marked" : "marked") + " with the arrow");
    }
    if (scenarioExpect_.contains("battleAnim") && battleRigState_ != scenarioExpect_["battleAnim"].get<std::string>())
        fail("the battle picture shows \"" + battleRigState_ + "\", expected \"" + scenarioExpect_["battleAnim"].get<std::string>() + "\"");
    if (scenarioExpect_.contains("condition") && !holds(scenarioExpect_["condition"]))
        fail("the condition " + scenarioExpect_["condition"].dump() + " does not hold");
    report = "[scenario] " + scenarioName_ + ": " + (ok ? "ok (" + scene + ")" : "FAILED" + out);
    return ok;
}

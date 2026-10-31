// game_combat.cpp — battle logic: start/finish, and Battle v2's action resolution
// (tap-to-freeze attack/defense bars, the enemy clock, super attack). Split out of game.cpp 2026-09-13.
#include "game_internal.h"

#include <limits>

using namespace toms::game_detail;

void Game::startCombat(const EnemyInst& e, const std::vector<EnemyInst>& party) {
    // the lead (the map's monster) and its party, up to three (docs/gameplay/13 §4.6)
    cs.foes.clear();
    cs.foes.reserve(toms::kMaxFoes);   // a summon never moves them (references stay good)
    auto add = [this](const EnemyInst& m) {
        CombatState::Foe f;
        f.enemy = m;
        if (f.enemy.mapId.empty()) f.enemy.mapId = m.id;
        f.hp = m.hp;
        f.events = toms::monsterEventsFromJson(m.events);
        cs.foes.push_back(f);
    };
    add(e);
    for (const EnemyInst& m : party)
        if ((int)cs.foes.size() < toms::kMaxFoes) add(m);
    // the party's clocks start a little apart, so they do not all attack at the same moment
    for (size_t i = 1; i < cs.foes.size(); i++) cs.foes[i].clockMs = -700 * (int)i;
    cs.target = 0;
    cs.events.clear();
    cs.clockMs = 0;
    // the revamp (docs/gameplay/15): no moment running, a fresh chain and grade, the guard unpressed
    cs.hitStopMs = cs.slowMs = cs.shakeMs = cs.dimMs = cs.bannerMs = 0;
    cs.slowScale = 1.0f; cs.slowCarry = 0.0f; cs.winPending = false; cs.shakePx = 0.0f;
    cs.banner.clear(); cs.bannerKind.clear();
    cs.chain = cs.bestChain = cs.perfects = cs.attacksDone = cs.damageTaken = 0;
    cs.guardAtMs = -1; cs.guardCdMs = 0; cs.ultimate = false;
    cs.guardLockMs = 0; cs.playDealt = 0; cs.skill = CombatState::SkillRun{};
    cs.camZoom = 1.0f; cs.camMs = cs.camMsMax = 0; cs.camOn = -1;
    cs.result = CombatState::Result{};
    cs.pace = effectivePace();
    cs.stamina = (float)paceSettings_.staminaMax;
    moveRng_.seed(20261025u);   // the same battle goes the same way
    playRng_.seed(scenarioTestFrames_ > 0 ? 20261031u : std::random_device{}());   // the plays: different every battle
    cs.playerHP = pl.hp; cs.active = true; cs.won = false;
    cs.atkBar = CombatState::AutoBar{};
    cs.defBar = CombatState::AutoBar{};
    cs.combo = toms::ComboPlay{};
    cs.touch = toms::TouchPlay{};
    battleBoxesKnown_ = false;   // the plays wait for this battle's layout (the screen reports it a frame later)
    cs.touchCharge = 0.0f;
    cs.touchPaid = false;
    touchHeld_ = false;
    cs.chargeMs = 0;
    cs.shieldBanked = false; cs.shieldPower = 0.0f;
    cs.superCharge = 0;
    cs.activeRuntime = toms::makeBattleRuntime(activeDefs_);
    cs.activeStatus = toms::ActiveStatusEffects{};
    cs.nextAttackGuaranteedCrit = false;
    cs.nextAttackCritDamageMult = 1.0f;
    cs.survivalArmed = false;
    cs.resultPauseMs = 0;
    cs.attackPauseMs = 0;
    cs.popups.clear();
    battleTimers_.reset();   // a number still waiting from the last fight never lands in this one
    battleFx_.clear();
    battleFxSeed_ = 0;
    foeRigs_.clear();        // the new monsters start idle (updateMonsterRig)
    foeRigs_.resize(cs.foes.size());
    cs.log = locale_.tr("battle.start");
    std::string ids;
    for (const CombatState::Foe& f : cs.foes) ids += (ids.empty() ? "" : " + ") + f.enemy.id;
    battleEvent("start", -1, -1, (int)cs.foes.size(), ids + " (" + toms::paceName(cs.pace) + ")");
    for (size_t i = 0; i < cs.foes.size(); i++) chooseFoeMove((int)i);
    // the monsters' first events: before the battle (its first line), then its start (docs/gameplay/13 §4.4)
    const std::string startLog = cs.log;
    for (size_t i = 0; i < cs.foes.size() && cs.active; i++) {
        toms::EventCheck c;
        c.on = "beforeBattle";
        runFoeEvents((int)i, c);
    }
    if (cs.log != startLog) cs.attackPauseMs = std::max(cs.attackPauseMs, 1200);   // a line to read before it starts
    for (size_t i = 0; i < cs.foes.size() && cs.active; i++) {
        toms::EventCheck c;
        c.on = "battleStart";
        runFoeEvents((int)i, c);
    }
}

// ---- the battle's rules (docs/gameplay/13; battle_rules.h) ----

// ---- the revamp: moments, words, the banner, the win (docs/gameplay/15_BATTLE_REVAMP.md) ----

// The battle's time this frame: a hit stop freezes it, the last hit's slow motion slows it (then the win). The
// screen's effects (shake, dim, banner, the guard's wait) run on real time.
int Game::battleFrame(int dtMs) {
    cs.shakeMs = std::max(0, cs.shakeMs - dtMs);
    cs.dimMs = std::max(0, cs.dimMs - dtMs);
    cs.bannerMs = std::max(0, cs.bannerMs - dtMs);
    cs.guardCdMs = std::max(0, cs.guardCdMs - dtMs);
    cs.guardLockMs = std::max(0, cs.guardLockMs - dtMs);
    cs.camMs = std::max(0, cs.camMs - dtMs);
    int bdt = dtMs;
    if (cs.hitStopMs > 0) {
        cs.hitStopMs = std::max(0, cs.hitStopMs - dtMs);
        bdt = 0;
    } else if (cs.slowMs > 0) {
        cs.slowMs -= dtMs;
        cs.slowCarry += dtMs * cs.slowScale;
        bdt = (int)cs.slowCarry;
        cs.slowCarry -= (float)bdt;
        if (cs.slowMs <= 0) {
            cs.slowMs = 0;
            if (cs.winPending) { cs.winPending = false; finishCombatWin(); }
        }
    }
    cs.frameDt = bdt;
    return bdt;
}

void Game::feelMoment(const std::string& m) {
    const bool reduced = settings_.reduceEffects;
    cs.hitStopMs = std::max(cs.hitStopMs, feel_.stop(m, reduced));
    const float px = feel_.shake(m, reduced);
    const float now = cs.shakeMs > 0 ? cs.shakePx * (float)cs.shakeMs / (float)cs.shakeMsMax : 0.0f;
    if (px > now) { cs.shakePx = px; cs.shakeMs = cs.shakeMsMax = std::max(1, feel_.shakeMs); }
    const std::string s = feel_.sound(m);
    if (!s.empty()) audio.play(s);
}

void Game::popWord(const std::string& text, int who, const std::string& style) {
    CombatState::Popup p;
    p.text = text; p.who = who; p.style = style; p.lift = 46;
    p.rise = toms::Tween(0.0f, 1.0f, 1100, toms::Ease::OutCubic);
    cs.popups.push_back(p);
}

void Game::showBanner(const std::string& text, const std::string& kind, int ms) {
    cs.banner = text; cs.bannerKind = kind;
    cs.bannerMs = cs.bannerMsMax = std::max(1, ms);
}

// The last monster fell (15 §3.8): the moment plays in slow motion, then the win and its result screen.
void Game::startWinMoment() {
    const int ms = settings_.reduceEffects ? 0 : (int)std::lround(feel_.slowMoMs * std::min(1.0f, feel_.strength));
    if (ms <= 0 || arena_.on) { finishCombatWin(); return; }   // (the arena: the next fight at once, as before)
    cs.winPending = true;
    cs.slowMs = ms;
    cs.slowScale = feel_.slowMoScale;
    cs.slowCarry = 0.0f;
}

void Game::setReduceEffects(bool on) {
    settings_.reduceEffects = on;
    toms::ensureSaveDir(toms::defaultSaveDir());
    toms::saveGameSettings(toms::settingsPath(), settings_);
}

toms::Pace Game::effectivePace() const {
    toms::Pace p = paceSettings_.pace;
    p = toms::paceFromString(weaponPlayOf(weaponKey()).pace, p);
    return toms::paceFromString(paceOverride_, p);
}

void Game::setBattlePace(const std::string& pace) {
    paceOverride_ = pace;
    if (!cs.active) return;
    const toms::Pace p = effectivePace();
    if (p == cs.pace) return;
    cs.pace = p;
    cs.atkBar.cooling = cs.defBar.cooling = false;   // switching: no wait carried over
    cs.stamina = (float)paceSettings_.staminaMax;
    battleEvent("pace", -1, -1, 0, toms::paceName(p));
}

int Game::foeBreakPoints(int foe) const {
    const int own = cs.foes[(size_t)foe].enemy.breakPoints;
    return own > 0 ? own : breakSettings_.points;
}

std::string Game::foeLogName(int foe) const {
    return foe >= 0 && foe < (int)cs.foes.size() ? cs.foes[(size_t)foe].enemy.name : std::string();
}

float Game::playerHpPct() const {
    return 100.0f * (float)std::max(0, cs.playerHP) / (float)std::max(1, effectiveMaxHp());
}

void Game::chooseFoeMove(int foe) {
    CombatState::Foe& f = cs.foes[(size_t)foe];
    const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(moveRng_);
    f.move = toms::pickMove(f.enemy.moves, roll);
    const toms::MoveDef& m = moves_.get(f.move);
    f.moveMs = m.chargeMs > 0 ? std::max(300, (int)std::lround(m.chargeMs * f.intervalMul))   // its wind-up in ms (16 §4.1)
                              : std::max(300, (int)std::lround(std::max(500, f.enemy.atkIntervalMs) * m.time * f.intervalMul));
    if (!f.enemy.moves.empty()) battleEvent("intent", foe, -1, f.moveMs, f.move);   // (a plain striker: always the same)
}

void Game::stepFoes(int dtMs) {
    for (size_t i = 0; i < cs.foes.size() && cs.active; i++) {
        const int foe = (int)i;
        {
            toms::EventCheck c;   // timed events run on battle time
            c.on = "everyMs";
            c.msBefore = cs.clockMs - dtMs;
            c.msAfter = cs.clockMs;
            if (!cs.foes[i].down()) runFoeEvents(foe, c);
        }
        if (!cs.active) return;
        CombatState::Foe& f = cs.foes[i];
        if (f.down()) continue;
        if (f.staggerMs > 0) {   // stunned: its clock stops
            f.staggerMs -= dtMs;
            if (f.staggerMs <= 0) { f.staggerMs = 0; f.staggerDamage = 1.0f; f.broken = false; }
            continue;
        }
        // the owner 2026-10-26: they keep charging while the player's combo runs (their attack lands in the middle of
        // it; quick jabs also cut it short). data/battle.json "combo_holds_monsters": true brings back the old wait.
        if (f.volleyHit > 0) {   // a run of hits (16 §4.1): the next one after its gap; the clock waits
            f.volleyMs -= dtMs;
            if (f.volleyMs <= 0) resolveEnemyClockFire(foe);
            continue;
        }
        if (comboHoldsMonsters_ && attackRunning() && moves_.get(f.move).kind != toms::MoveKind::Jabs) continue;
        f.clockMs += dtMs;
        if (f.clockMs >= f.moveMs) {
            f.clockMs = 0;
            resolveEnemyClockFire(foe);
        }
    }
}

void Game::stunFoe(int foe, int ms, float damage, bool broken) {
    if (ms <= 0) return;
    CombatState::Foe& f = cs.foes[(size_t)foe];
    f.staggerMs = std::max(f.staggerMs, ms);
    f.staggerDamage = std::max(f.staggerDamage, damage);
    f.broken = f.broken || broken;
    battleEvent(broken ? "break" : "stagger", foe, -1, ms);
    foeLook(foe, broken ? "broken" : "parry");
    if (broken) {   // the break is a moment (15 §3.4): the banner, the screen dims, a stop and a shake
        showBanner(locale_.tr("battle.word_break"), "break", 1300);
        cs.dimMs = settings_.reduceEffects ? 0 : 700;
        feelMoment("break");
    }
}

void Game::damageFoe(int foe, int dmg) {
    CombatState::Foe& f = cs.foes[(size_t)foe];
    const float maxHp = (float)std::max(1, f.enemy.hp);
    const float before = 100.0f * (float)std::max(0, f.hp) / maxHp;
    f.hp -= dmg;
    toms::EventCheck c;
    c.on = "hpBelow";
    c.hpPctBefore = before;
    c.hpPctAfter = 100.0f * (float)std::max(0, cs.foes[(size_t)foe].hp) / maxHp;
    runFoeEvents(foe, c);
    if (cs.active && cs.foes[(size_t)foe].hp <= 0) {   // its last words, or a heal that stands it up again
        toms::EventCheck d;
        d.on = "falls";
        runFoeEvents(foe, d);
    }
}

void Game::runFoeEvents(int foe, const toms::EventCheck& check) {
    if (foe < 0 || foe >= (int)cs.foes.size()) return;
    std::vector<toms::MonsterEvent>& evs = cs.foes[(size_t)foe].events;
    std::vector<nlohmann::json> todo;   // copied: an action may replace the monster and its events
    for (int idx : toms::fireMonsterEvents(evs, check)) {
        battleEvent("event", foe, -1, idx, check.on);
        todo.push_back(evs[(size_t)idx].actions);
    }
    for (const nlohmann::json& a : todo) {
        if (!cs.active) return;
        runFoeActions(foe, a);
    }
}

void Game::runFoeActions(int foe, const nlohmann::json& actions) {
    if (!actions.is_array()) return;
    bool fled = false;
    for (const nlohmann::json& a : actions) {
        if (!a.is_object() || !cs.active) continue;
        CombatState::Foe& f = cs.foes[(size_t)foe];
        if (a.contains("say") && a["say"].is_string()) {
            const std::string s = a["say"].get<std::string>();
            cs.log = locale_.has(s) ? locale_.tr(s) : s;
            showBanner(cs.log, "line", 2000);   // a phase change is a moment too (15 §3.5)
            feelMoment("phase");
            battleEvent("say", foe, -1, 0, s);
        }
        if (a.contains("stat") && a["stat"].is_string()) {
            const std::string st = a["stat"].get<std::string>();
            const float mul = a.value("mul", 1.0f);
            const int add = a.value("add", 0);
            if (st == "atk") { f.atkMul *= mul; f.enemy.atk += add; }
            else if (st == "def") { f.defMul *= mul; f.enemy.def += add; }
            else if (st == "interval") { f.intervalMul *= mul; f.enemy.atkIntervalMs += add; }
            battleEvent("stat", foe, -1, add, st + " x" + std::to_string(mul).substr(0, 4));
        }
        if (a.contains("transformInto") && a["transformInto"].is_string()) {
            EnemyInst n;
            const std::string id = a["transformInto"].get<std::string>();
            if (enemyFromData(id, nlohmann::json::object(), n)) {
                const std::string from = f.enemy.name;
                const float share = (float)std::max(0, f.hp) / (float)std::max(1, f.enemy.hp);
                n.x = f.enemy.x; n.y = f.enemy.y; n.mapId = f.enemy.mapId;
                f.hp = a.value("hp", std::string("keepShare")) == "full" ? n.hp
                                                                        : std::max(1, (int)std::ceil(share * (float)n.hp));
                f.enemy = n;
                f.events = toms::monsterEventsFromJson(n.events);
                f.atkMul = f.defMul = f.intervalMul = 1.0f;
                f.breakGauge = 0.0f;
                f.clockMs = 0;
                chooseFoeMove(foe);
                if (foe < (int)foeRigs_.size()) foeRigs_[(size_t)foe].state.clear();   // the new monster's clips
                foeLook(foe, "transform");
                cs.log = trParam(trParam(locale_.tr("battle.transform"), "name", from), "to", n.name);
                battleEvent("transform", foe, -1, f.hp, id);
            }
        }
        if (a.contains("heal")) {
            const int heal = (int)std::lround(f.enemy.hp * a.value("heal", 0.0f) / 100.0f);
            f.hp = std::min(f.enemy.hp, std::max(0, f.hp) + heal);
            battleEvent("heal", foe, -1, heal);
        }
        if (a.contains("summon") && a["summon"].is_string() && (int)cs.foes.size() < toms::kMaxFoes) {
            EnemyInst n;
            if (enemyFromData(a["summon"].get<std::string>(), nlohmann::json::object(), n)) {
                CombatState::Foe s;
                s.enemy = n;
                s.hp = n.hp;
                s.events = toms::monsterEventsFromJson(n.events);
                cs.foes.push_back(s);
                foeRigs_.resize(cs.foes.size());
                chooseFoeMove((int)cs.foes.size() - 1);
                cs.log = trParam(locale_.tr("battle.summon"), "name", n.name);
                battleEvent("summon", foe, (int)cs.foes.size() - 1, 0, n.id);
            }
        }
        if (a.value("flee", false)) {
            CombatState::Foe& g = cs.foes[(size_t)foe];
            g.fled = true;
            g.hp = 0;
            fled = true;
            cs.log = trParam(locale_.tr("battle.flee"), "name", g.enemy.name);
            battleEvent("flee", foe, -1);
        }
        if (a.contains("anim") && a["anim"].is_string()) foeLook(foe, a["anim"].get<std::string>());
        if (a.contains("fx") && a["fx"].is_string()) playBattleFx(a["fx"].get<std::string>(), foe);
        if (a.contains("sound") && a["sound"].is_string()) audio.play(a["sound"].get<std::string>());
        if (a.contains("flag") && a["flag"].is_string()) toms::setStoryFlag(meta_, a["flag"].get<std::string>());
        if (a.contains("dialogue"))   // (not yet: a dialogue in the middle of a battle)
            std::fprintf(stderr, "[battle] %s: a \"dialogue\" action is not supported yet; use \"say\"\n",
                         cs.foes[(size_t)foe].enemy.id.c_str());
    }
    if (fled && cs.active) {   // it left: the battle may be over, or the target moves on
        if (toms::allDown(cs.foeHp())) finishCombatWin();
        else if (cs.foes[(size_t)cs.target].down()) cs.target = toms::nextStanding(cs.foeHp(), cs.target, +1);
    }
}

void Game::debugForceWin() {
    if (!cs.active) return;
    for (CombatState::Foe& f : cs.foes) f.hp = std::min(f.hp, 0);
    battleEvent("win", -1, -1, 0, "by hand");
    finishCombatWin();
}

void Game::debugUpdateMonster(const std::string& id, const nlohmann::json& m) {
    if (!cs.active || !m.is_object()) return;
    for (size_t i = 0; i < cs.foes.size(); i++) {
        CombatState::Foe& f = cs.foes[i];
        if (f.enemy.id != id || f.down()) continue;
        EnemyInst& e = f.enemy;
        if (m.contains("name")) e.name = locale_.field(m["name"]);
        const int maxHp = std::max(1, m.value("hp", e.hp));
        if (maxHp != e.hp) {   // its HP keeps its share of the new top
            f.hp = std::max(1, (int)std::lround((double)f.hp * maxHp / std::max(1, e.hp)));
            e.hp = maxHp;
        }
        e.atk = m.value("atk", e.atk);
        e.def = m.value("def", e.def);
        e.exp = m.value("exp", e.exp);
        e.gold = m.value("gold", e.gold);
        e.look = m.value("look", std::string("monster"));
        e.breakPoints = m.value("break", 0);
        e.moves = toms::moveChoicesFromJson(m.value("moves", nlohmann::json::array()));   // from its next move on
        const int interval = m.value("atk_interval_ms", 4000);
        if (interval != e.atkIntervalMs) {   // the wind-up under way keeps how far along it is
            const double done = f.moveMs > 0 ? std::clamp((double)f.clockMs / f.moveMs, 0.0, 1.0) : 0.0;
            e.atkIntervalMs = interval;
            const toms::MoveDef& mv = moves_.get(f.move);
            f.moveMs = std::max(300, (int)std::lround(std::max(500, e.atkIntervalMs) * mv.time * f.intervalMul));
            if (f.clockMs >= 0) f.clockMs = (int)std::lround(done * f.moveMs);
        }
        const nlohmann::json ev = m.value("events", nlohmann::json::array());
        if (ev != e.events) { e.events = ev; f.events = toms::monsterEventsFromJson(ev); }   // fresh: none fired yet
        battleEvent("edited", (int)i, -1, e.atkIntervalMs, "by the editor");
    }
}

void Game::debugFireFoeEvent(int foe, int event) {
    if (!cs.active || foe < 0 || foe >= (int)cs.foes.size()) return;
    const std::vector<toms::MonsterEvent>& evs = cs.foes[(size_t)foe].events;
    if (event < 0 || event >= (int)evs.size()) return;
    battleEvent("event", foe, -1, event, "by hand");
    const nlohmann::json actions = evs[(size_t)event].actions;
    runFoeActions(foe, actions);
}

void Game::foeLook(int foe, const std::string& state) {
    if (foe < 0 || foe >= (int)cs.foes.size()) return;
    if (const toms::LookState* ls = looks_.find(cs.foes[(size_t)foe].enemy.look, state)) {
        if (!ls->sound.empty()) audio.play(ls->sound);
        if (!ls->fx.empty()) playBattleFx(ls->fx, foe);
    }
    if (state != "idle" && foe < (int)foeRigs_.size()) foeRigs_[(size_t)foe].request = state;
}

void Game::playerLook(const std::string& state) {
    if (const toms::LookState* ls = looks_.find("player", state, "player")) {
        if (!ls->sound.empty()) audio.play(ls->sound);
        if (!ls->fx.empty()) playBattleFx(ls->fx, -1);
    }
}

void Game::interruptCombo(int foe) {
    if (!attackRunning()) return;
    cs.combo = toms::ComboPlay{};
    cs.touch.stop();
    cs.chargeMs = 0;
    if (cs.pace == toms::Pace::Cooldown) { cs.atkBar.cooling = true; cs.atkBar.cooldownMs = barCooldownMs(); }
    if (battleRigReady()) setBattleRigState("miss", true);
    cs.log = trParam(locale_.tr("battle.interrupted"), "name", foeLogName(foe));
    battleEvent("interrupt", foe, -1);
}

// ---- the battle's log (docs/gameplay/13 §4.5) and its monsters ----

void Game::battleEvent(const std::string& type, int who, int whom, int value, const std::string& note) {
    toms::BattleEvent e;
    e.ms = cs.clockMs; e.type = type; e.who = who; e.whom = whom; e.value = value; e.note = note;
    cs.events.push_back(e);
    if (battleLogToStderr_) {
        std::vector<std::string> ids;
        for (const CombatState::Foe& f : cs.foes) ids.push_back(f.enemy.id);
        std::fprintf(stderr, "[battle] %s\n", toms::battleEventLine(e, ids).c_str());
    }
}

void Game::battleSetTarget(int foe) {
    if (!cs.active || foe < 0 || foe >= (int)cs.foes.size() || cs.foes[(size_t)foe].down() || foe == cs.target) return;
    cs.target = foe;
    battleEvent("target", -1, foe);
    audio.play("confirm_click");
}

void Game::battleCycleTarget(int dir) {
    if (!cs.active || cs.foes.size() < 2) return;
    const int next = toms::nextStanding(cs.foeHp(), cs.target, dir);
    if (next >= 0) battleSetTarget(next);
}

// A foe fell (the battle goes on: the caller has checked that some still stand): it plays its die clip and stays down,
// and the target moves to the next one standing.
void Game::foeDown(int foe) {
    battleEvent("down", foe, -1, 0, cs.foes[(size_t)foe].enemy.id);
    foeLook(foe, "die");
    if (foe == cs.target) {
        const int next = toms::nextStanding(cs.foeHp(), foe, +1);
        if (next >= 0) { cs.target = next; battleEvent("target", -1, next); }
    }
}

// ---- the battle rig (Game::battleRigState) ----

void Game::loadBattleRig() {
    if (battleRigWeapon_ == equipped_.weaponId && (battleCharacterAnim_ || battleHeldAnim_ || battleRigWeapon_.empty())) return;
    battleRigWeapon_ = equipped_.weaponId;
    battleCharacterAnim_.reset();
    battleHeldAnim_.reset();
    battleRig_.stop();
    battleRigState_.clear();
    auto it = equipmentDefs_.find(equipped_.weaponId);
    if (it == equipmentDefs_.end() || !res_) return;
    auto load = [this](const std::string& path) -> std::shared_ptr<toms::res::AnimRes> {
        if (path.empty()) return nullptr;
        auto a = res_->get<toms::res::AnimRes>(path);
        if (!a->ok()) { std::fprintf(stderr, "[battle] %s: %s\n", path.c_str(), a->error().c_str()); return nullptr; }
        return a;
    };
    battleCharacterAnim_ = load(it->second.battleCharacterAnim);
    battleHeldAnim_ = load(it->second.battleHeldAnim);
}

bool Game::battleRigReady() const { return battleCharacterAnim_ || battleHeldAnim_; }

void Game::setBattleRigState(const std::string& state, bool restart, const std::string& clip) {
    if (state == battleRigState_ && !restart) return;
    battleRigState_ = state;
    // The weapon's clip for this status (items.json "battleAnim" "clips"; docs/gameplay/07 phase 2), else the status's own name.
    auto def = equipmentDefs_.find(equipped_.weaponId);
    const std::string clipName = !clip.empty() ? clip : def != equipmentDefs_.end() ? def->second.battleClip(state) : state;
    const toms::anim::Clip* body = battleCharacterAnim_ ? battleCharacterAnim_->find(clipName) : nullptr;
    if (body) {   // the player's character in the clip made with another
        battleFitted_ = fitClipToCharacter(*body, playerSprite());
        body = &battleFitted_;
    }
    const toms::anim::Clip* held = battleHeldAnim_ ? battleHeldAnim_->find(clipName) : nullptr;
    battleRigClip_ = body || held ? clipName : std::string();   // what plays (a test scenario checks it)
    battleRig_.play(body, held);
}

void Game::updateBattleRig(int dtMs) {
    const bool shown = cs.active || cs.won || cs.resultPauseMs > 0;
    if (!shown) {
        if (!battleRigState_.empty()) { battleRig_.stop(); battleRigState_.clear(); battleRigClip_.clear(); }
        return;
    }
    loadBattleRig();
    if (!battleRigReady()) return;
    // An attack or a miss plays to its end; then the bar decides: waiting for the tap = charge, recovering = idle.
    const bool shooting = (battleRigState_ == "attack" || battleRigState_ == "miss") && !battleRig_.finished();
    if (!shooting) setBattleRigState(cs.active && !cs.atkBar.cooling ? "charge" : "idle");
    battleRig_.update(dtMs);
    battleRig_.takeEvents();   // (release / hit / miss: the battle's own timers place the numbers)
}

void Game::drawBattleRig() {
    static const toms::anim::AtlasSet kNone;
    // The battle screen's backdrop (common.rcss .focus: black at 80%). The UI draws over the game, so with the backdrop
    // in battle.rml the player and the monster would sit under it, drawn at a fifth of their brightness; it is drawn
    // here instead, under them (battle.rml has none).
    Quad backdrop;
    backdrop.rect[0] = -4096.0f; backdrop.rect[1] = -4096.0f; backdrop.rect[2] = 16384.0f; backdrop.rect[3] = 16384.0f;
    backdrop.uv[0] = 0; backdrop.uv[1] = 0; backdrop.uv[2] = 1; backdrop.uv[3] = 1;
    backdrop.solid = true;
    backdrop.tint[0] = backdrop.tint[1] = backdrop.tint[2] = 0.0f;
    backdrop.tint[3] = std::min(1.0f, 0xcc / 255.0f + 0.15f * (float)cs.dimMs / 700.0f);   // a break dims it a moment
    if (ren) ren->drawSprite(backdrop);
    // the screen shake (docs/gameplay/15 §3.8): the fighters and their effects move together, fading out
    float ox = 0.0f, oy = 0.0f;
    if (cs.shakeMs > 0 && cs.shakePx > 0.0f) {
        const float amp = cs.shakePx * (float)cs.shakeMs / (float)cs.shakeMsMax;
        std::uniform_real_distribution<float> u(-amp, amp);
        ox = u(shakeRng_); oy = u(shakeRng_);
    }
    // the camera's close-up (docs/gameplay/16 §6.1): the fighters and their effects scaled about the one it is on
    float zoom = 1.0f;
    glm::vec2 focus(0.0f);
    if (cs.camMs > 0 && cs.camZoom > 1.0f) {
        const float in = std::min(1.0f, (float)(cs.camMsMax - cs.camMs) / 200.0f), out = std::min(1.0f, (float)cs.camMs / 300.0f);
        const float k = std::min(in, out);
        zoom = 1.0f + (cs.camZoom - 1.0f) * k * (2.0f - k);
        const glm::vec4 fb = fighterBox(cs.camOn);
        focus = {fb.x + fb.z * 0.5f, fb.y + fb.w * 0.5f};
    }
    auto zoomed = [&](glm::vec4 b) {
        return glm::vec4(focus.x + (b.x - focus.x) * zoom + ox, focus.y + (b.y - focus.y) * zoom + oy, b.z * zoom, b.w * zoom);
    };
    // The player's place on the battle screen (battle.rcss #player, wherever the layout put it: playerBox_): the feet
    // at its bottom centre, the 32-pixel character filling its height.
    const glm::vec4 pb = zoomed(playerBox_);
    if (battleRigReady() && !battleRigState_.empty())
        battleRig_.draw(ren, battleCharacterAnim_ ? battleCharacterAnim_->atlases() : kNone,
                        battleHeldAnim_ ? battleHeldAnim_->atlases() : kNone,
                        toms::anim::placement(pb.x + pb.z / 2.0f, pb.y + pb.w, pb.w / 32.0f));
    // Each monster in its box (battle.rcss .foe .fbox: foeBox_): its sprite's longer side fills the box (a 2-wide golem
    // as wide as the box), feet at the bottom centre, as the picture it replaces.
    for (size_t i = 0; i < foeRigs_.size() && i < cs.foes.size(); i++) {
        const FoeRig& fr = foeRigs_[i];
        if (!monsterRigReady() || fr.state.empty()) continue;
        const glm::vec4 eb = zoomed(fighterBox((int)i));
        const toms::AtlasRegion* r = spriteAtlas_.find(monsterSprite((int)i));
        const float longer = r ? (float)std::max(std::max(r->origW, r->origH), 1) : 32.0f;
        fr.rig.draw(ren, monsterAnim_->atlases(), kNone,
                    toms::anim::placement(eb.x + eb.z / 2.0f, eb.y + eb.w, std::min(eb.z, eb.w) / longer));
    }
    if (battleFxFile_) {
        glm::mat3 shake(1.0f);   // (and the close-up's scale)
        shake[0][0] = shake[1][1] = zoom;
        shake[2][0] = focus.x * (1.0f - zoom) + ox; shake[2][1] = focus.y * (1.0f - zoom) + oy;
        for (BattleFx& b : battleFx_)
            if (!b.overUi) b.fx.draw(ren, battleFxFile_->atlases(), shake, nullptr);
        ren->setLayer(1);   // the track's hints: over the battle screen's UI
        for (BattleFx& b : battleFx_)
            if (b.overUi) b.fx.draw(ren, battleFxFile_->atlases(), glm::mat3(1.0f), nullptr);
        ren->setLayer(0);
    }
}

// ---- the battle's particles (Game::battleFx_) ----

const toms::WeaponFx* Game::battleWeaponFx(bool top) const {
    const std::string key = weaponKey();
    const int lv = top ? std::numeric_limits<int>::max() : weaponLevel(key);
    if (const toms::WeaponFx* f = toms::weaponFxEntry(weaponPlayOf(key), lv)) return f;
    return toms::weaponFxEntry(weaponPlay_.get("default"), lv);   // a weapon without its own: the default's
}

// One-shot, at the player's weapon (a little ahead of the character, in its box) or a monster's middle.
void Game::playBattleFx(const std::string& effect, int who, int delayMs) {
    const glm::vec4 b = fighterBox(who);
    if (who >= 0) playBattleFxAt(effect, b.x + b.z * 0.5f, b.y + b.w * 0.5f, false, delayMs);
    else playBattleFxAt(effect, b.x + b.z * 0.731f, b.y + b.w * 0.493f, false, delayMs);
}

void Game::playBattleFxAt(const std::string& effect, float x, float y, bool overUi, int delayMs) {
    if (effect.empty()) return;
    if (!battleFxFile_ && !battleFxTried_ && res_) {   // once: without the file the battle has no particles
        battleFxTried_ = true;
        auto f = res_->get<toms::res::ParticleRes>(kBattleFxFile);
        if (f->ok()) battleFxFile_ = f;
        else std::fprintf(stderr, "[battle] %s: %s\n", kBattleFxFile, f->error().c_str());
    }
    if (!battleFxFile_) return;
    const toms::fx::Effect* e = battleFxFile_->find(effect);
    if (!e) { std::fprintf(stderr, "[battle] %s has no effect '%s'\n", kBattleFxFile, effect.c_str()); return; }
    auto start = [this, e, x, y, overUi] {
        BattleFx b;
        b.overUi = overUi;
        b.fx.setTransform(toms::anim::placement(x, y));
        if (!overUi) b.fx.setGpuSimulation(ren, particleGpuThreshold());   // the top layer draws quads only
        b.fx.play(e, ++battleFxSeed_);
        battleFx_.push_back(std::move(b));
    };
    if (delayMs > 0) timers_.after(battleTimers_, delayMs, start);   // dies with the battle, as the damage numbers
    else start();
}

void Game::updateBattleFx(int dtMs) {
    if (!(cs.active || cs.won || cs.resultPauseMs > 0)) { battleFx_.clear(); return; }
    for (BattleFx& b : battleFx_) b.fx.update(dtMs / 1000.0f);
    battleFx_.erase(std::remove_if(battleFx_.begin(), battleFx_.end(), [](const BattleFx& b) { return b.fx.finished(); }),
                    battleFx_.end());
}

// The marker came into a point's window (hit = false): a sound and a flash on the point, "tap now". A point hit: a
// burst on it. On the track's points as the UI laid them out (setComboPointsOnScreen).
void Game::comboHint(int point, bool hit) {
    if (!hit) audio.play("combo_ready");
    if (point < 0 || point >= (int)comboPointsOnScreen_.size()) return;
    const glm::vec2 p = comboPointsOnScreen_[(size_t)point];
    playBattleFxAt(hit ? "combo_point_hit" : "combo_ready", p.x, p.y, /*overUi=*/true);
}

// ---- the monsters on the battle screen (Game::foeRigs_) ----

std::string Game::monsterSprite(int foe) const {
    if (foe < 0 || foe >= (int)cs.foes.size()) return std::string();
    const EnemyInst& e = cs.foes[(size_t)foe].enemy;
    return e.boss ? std::string("boss_demonlord") : typeSprite("monster", e.id);
}

void Game::setFoeRigState(int foe, const std::string& state) {
    FoeRig& fr = foeRigs_[(size_t)foe];
    fr.state = state;
    // the look's clip for this status (data/looks.json), the monster's own version of it first, else the shared one
    const std::string& id = cs.foes[(size_t)foe].enemy.id;
    const toms::LookState* ls = looks_.find(cs.foes[(size_t)foe].enemy.look, state);
    const std::string clip = ls && !ls->clip.empty() ? ls->clip : state;
    const toms::anim::Clip* c = monsterAnim_->find(clip + "_" + id);
    if (!c) c = monsterAnim_->find(clip);
    if (!c) c = monsterAnim_->find(state);
    fr.clip = c ? c->name : std::string();
    if (!c) { fr.rig.stop(); return; }
    fr.fitted = fitClipToCharacter(*c, monsterSprite(foe));
    fr.rig.play(&fr.fitted, nullptr);
}

void Game::updateMonsterRig(int dtMs) {
    const bool shown = cs.active || cs.won || cs.resultPauseMs > 0;
    if (!shown) {
        for (FoeRig& fr : foeRigs_) { fr.rig.stop(); fr.state.clear(); fr.clip.clear(); fr.request.clear(); }
        return;
    }
    if (!monsterAnim_ && !monsterAnimTried_ && res_) {   // once: without the file the UI's picture stays
        monsterAnimTried_ = true;
        auto a = res_->get<toms::res::AnimRes>(kMonsterBattleAnim);
        if (a->ok()) monsterAnim_ = a;
        else std::fprintf(stderr, "[battle] %s: %s\n", kMonsterBattleAnim, a->error().c_str());
    }
    if (!monsterRigReady()) return;
    if (foeRigs_.size() != cs.foes.size()) foeRigs_.resize(cs.foes.size());
    for (size_t i = 0; i < foeRigs_.size(); i++) {
        FoeRig& fr = foeRigs_[i];
        const int foe = (int)i;
        if (!fr.request.empty() && fr.state != "die") {   // an attack, a hit or its fall: from the start
            const std::string req = fr.request;
            fr.request.clear();
            setFoeRigState(foe, req);
        } else {
            fr.request.clear();
            // every status but idle plays to its end; then the fight decides: down or won = die (it stays gone), else idle
            const bool oneShot = !fr.state.empty() && fr.state != "idle";
            if (fr.state != "die" && !(oneShot && !fr.rig.finished())) {
                const std::string next = cs.won || cs.foes[i].down() ? "die" : "idle";
                if (next != fr.state) setFoeRigState(foe, next);
            }
        }
        fr.rig.update(dtMs);
        fr.rig.takeEvents();   // (hit: the battle's own timers place the numbers)
    }
}

// The owner's rule (docs/gameplay/07_GAMEPLAY_PLAN.md §4): an attack pauses both sides' clocks until it is over.
void Game::startAttackPause() { cs.attackPauseMs = std::max(cs.attackPauseMs, attackPauseMs_); }

// The number appears when the swing lands, not when the button is pressed: a safe timer owned by this battle.
void Game::popDamage(int dmg, int who, const std::string& style) {
    timers_.after(battleTimers_, hitNumberDelayMs_, [this, dmg, who, style] {
        CombatState::Popup p;
        p.text = dmg > 0 ? std::to_string(dmg) : locale_.tr("battle.popup_guard");
        p.who = who;
        p.style = dmg > 0 ? style : "guard";
        p.rise = toms::Tween(0.0f, 1.0f, 900, toms::Ease::OutCubic);
        cs.popups.push_back(p);
        // the monster flinches as the number appears (a fallen one keeps falling)
        if (who >= 0 && who < (int)foeRigs_.size() && who < (int)cs.foes.size() && dmg > 0 &&
            foeRigs_[(size_t)who].request != "die" && !cs.foes[(size_t)who].down())
            foeLook(who, "hurt");
    });
}

// Shared win/lose resolution -- identical to the pre-Milestone-6 auto-combat's own win/lose
// handling (rewards, level-up, boss warp, respawn), just extracted so resolveAttackTap/
// battleTapSuper (win) and resolveEnemyClockFire (lose) can all reach it without duplicating it.
void Game::finishCombatWin() {
    if (arenaEndFight(true)) return;   // the weapon test arena: no rewards, the next fight follows
    cs.active = false; cs.won = true; cs.winPending = false;
    battleEvent("win", -1, -1);
    pl.hp = cs.playerHP;
    // the grade (docs/gameplay/15 §3.6): the time, the HP lost, the perfects and the chain
    CombatState::Result& res = cs.result;
    res = CombatState::Result{};
    res.valid = true;
    res.seconds = cs.clockMs / 1000.0f;
    {
        toms::GradeInput gi;
        gi.seconds = res.seconds;
        gi.lostShare = (float)cs.damageTaken / (float)std::max(1, effectiveMaxHp());
        gi.perfectRate = cs.attacksDone > 0 ? (float)cs.perfects / (float)cs.attacksDone : 0.0f;
        gi.bestChain = cs.bestChain;
        res.grade = toms::battleGrade(gi, grade_, &res.score);
    }
    const bool gradeS = res.grade == "S";
    const std::string winSound = feel_.sound("win");
    if (!winSound.empty()) audio.play(winSound);
    int gold = 0, exp = 0;   // every monster of the battle pays
    for (const CombatState::Foe& f : cs.foes)
        if (!f.fled) { gold += f.enemy.gold; exp += f.enemy.exp; }
    res.gold = (int)std::lround(gold * (1.0f + attrEffect("goldGain") / 100.0f) * (gradeS ? 1.0f + grade_.sGold : 1.0f));   // LUK, an S
    res.exp = exp;
    pl.gold += res.gold;
    gainExp(exp);   // level-ups by data/stats.json
    // Monster cards (docs/gameplay/07_GAMEPLAY_PLAN.md §3): each win is counted, and each card may drop (enemies.json "card").
    for (const CombatState::Foe& f : cs.foes) {
        auto t = enemyTpl.find(f.enemy.id);
        if (t == enemyTpl.end() || f.fled) continue;
        float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(cardRng_);
        if (gradeS) roll *= 1.0f - grade_.sCard;   // an S: a likelier card
        if (toms::recordCardWin(meta_.cards, f.enemy.id, toms::cardChanceFromJson(t->second), roll)) {
            const int n = meta_.cards[f.enemy.id].copies;   // (the result screen says it; no notice over it)
            if (res.card.empty())
                res.card = n == 1 ? trParam(locale_.tr("cards.new"), "name", f.enemy.name)
                                  : trParam(trParam(locale_.tr("cards.more"), "name", f.enemy.name), "n", std::to_string(n));
            newsCards_ = true;
        }
    }
    // remove the lead's entity from the stage -- only when it stands on the map (a test scenario's enemy does not: x, y -1;
    // its party never does)
    const EnemyInst& lead = cs.lead().enemy;
    const bool onMap = lead.y >= 0 && lead.y < (int)st.tiles.size() && lead.x >= 0 && lead.x < (int)st.tiles[lead.y].size();
    if (onMap) {
        for (auto& e : st.entities) if (e.x==lead.x && e.y==lead.y && e.id==lead.mapId) { e.consumed=true; fireLinkedEvents(e); }
        st.tiles[lead.y][lead.x] = '.';
        // Milestone 7: persist the clear so it survives a reload of this floor (stairs or the Stage
        // Select hub) -- see entityStatus_'s declaration in game.h.
        toms::setEntityStatus(entityStatus_, statusKeyAt(lead.x, lead.y), toms::EntityStatus::Defeated);
    }
    {   // the weapon's win (docs/gameplay/07 phase 1): enough of them raise its level -- more points, bigger bonuses
        const std::string key = weaponKey();
        const int before = weaponLevel(key);
        weaponUses_[key]++;
        const int after = weaponLevel(key);
        res.weapon = key; res.lvBefore = before; res.lvAfter = after; res.uses = weaponUses_[key];   // (the result shows it)
    }
    bool boss = false;
    for (const CombatState::Foe& f : cs.foes) {
        if (f.fled) continue;
        toms::globalEventBus().publish(toms::EnemyDefeated{f.enemy.id, curStage});
        boss = boss || f.enemy.boss;
    }
    if (boss) { cs.log = locale_.tr("battle.boss_win"); loadStage("stage_11"); }
    markProgressDirty();   // gold/exp/level/entity-status all changed: autosave will flush it
}

void Game::finishCombatLose() {
    if (arenaEndFight(false)) return;   // the weapon test arena: no death counted, no respawn
    cs.active = false; cs.won = false;
    battleEvent("lose", -1, -1);
    // M7 (first slice): noteDeath() existed since S3 (schemaVersion 3's deathsTotal/deathsNonBoss)
    // but nothing ever actually called it -- a real, pre-existing gap found while scoping this
    // slice, since e_13's condition depends entirely on this counter being real.
    bool boss = false;
    for (const CombatState::Foe& f : cs.foes) {
        boss = boss || f.enemy.boss;
        toms::recordCardLoss(meta_.cards, f.enemy.id);   // each monster's card record counts it
    }
    run_.noteDeath(boss);
    // docs/story/STORY_DATA_SCHEMA.md §7.3's wipe row: only e_10 (cycleIndex>=9 -- reachable now
    // that M8's rebirth actually increments it) and e_13 (12 non-boss deaths) are ever checked on a
    // plain wipe -- never the full 15-ending resolver (see checkNamedEndings's own comment for why:
    // the guaranteed fallback ending would otherwise wrongly end the run on literally every death).
    GameConditionContext endingCtx(pl, meta_, missionTrackers_, run_, equipped_);
    const toms::EndingDefinition* ending = toms::checkNamedEndings(endingsTable_, {"e_10", "e_13"}, endingCtx);
    if (ending) {
        triggerEnding(ending->id);
        return;   // the run is over -- no respawn, no "you fell" pause
    }
    cs.resultPauseMs = 300;   // keep the "you fell" message on screen briefly (see CombatState)
    pl.hp = effectiveMaxHp()/2; // respawn at stage start
    loadStage(curStage); // reset monsters/items
    markProgressDirty();
}

// Attack bar tapped: apply damage to the enemy from wherever its auto-moving marker currently is
// (docs/design/FIGHT_SCENE_DESIGN.md §4's power_mult curve, with the equipped weapon's maxMult and
// Berserker's Red-zone-deals-0 rule per docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.2). A kill ends the fight
// immediately -- there's no "the enemy still gets to retaliate this round" any more, since the
// enemy's own clock is what decides when it attacks, independent of this tap.
void Game::resolveAttackTap() {
    toms::PowerBarParams bar = attackBarParams();   // DEX widens its hit zone
    float pos = cs.atkBar.pos;
    // S7 (equipment actives): an armed "guaranteed_crit_next_attack" bypasses the real marker
    // position entirely -- it's a scripted perfect release, not a well-timed one, so it also
    // bypasses the Berserker red-zone-zeroes-attacks rule below (a badly timed tap couldn't
    // otherwise ever be "guaranteed").
    bool activeCrit = cs.nextAttackGuaranteedCrit;
    float power = activeCrit ? 100.0f : toms::powerFromPosition(bar, pos);
    float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    const int effAtk = effectiveAtk();   // equipment, skills (half after a rebirth), STR
    CombatState::Foe& foe = cs.foes[(size_t)cs.target];
    int baseHit = std::max(1, effAtk - (int)std::lround(foe.enemy.def * foe.defMul));
    int dmg = (int)std::lround(toms::computeAttackDamage(baseHit, power, maxMult) * foe.staggerDamage);
    if (!activeCrit && toms::zoneFromPosition(bar, pos) == toms::PowerZone::Red && toms::talentZerosRedZoneAttacks(equipped_.talentId))
        dmg = 0;   // Berserker: a high ceiling, but Red-zone releases deal nothing
    if (activeCrit) {
        dmg = (int)std::lround(dmg * cs.nextAttackCritDamageMult);
        cs.nextAttackGuaranteedCrit = false;
        cs.nextAttackCritDamageMult = 1.0f;
    }

    battleEvent("hit", -1, cs.target, dmg);
    damageFoe(cs.target, dmg);
    lastAttackDamage_ = dmg;   // the battle rig shows attack or miss
    if (dmg > 0) cs.superCharge = std::min(run_.superMax(), cs.superCharge + 1);   // M8: superMax is per-run, halved by rebirth()
    playerLook("attack");
    startAttackPause();
    popDamage(dmg, cs.target, "hit");
    afterPlayerHit(trParam(locale_.tr("battle.hit"), "dmg", std::to_string(dmg)), trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg)));
}

// After the player's hit on the target: it may have fallen. The last one standing falling wins the battle (winLog);
// another falling moves the target on (the log says who fell); else the hit's own log.
bool Game::afterPlayerHit(const std::string& hitLog, const std::string& winLog) {
    if (!cs.active) return true;   // a monster event already ended it (the last one fled)
    const int t = cs.target;
    if (!cs.foes[(size_t)t].down()) { cs.log = hitLog; return false; }
    if (toms::allDown(cs.foeHp())) {
        battleEvent("down", t, -1, 0, cs.foes[(size_t)t].enemy.id);
        cs.log = winLog;
        foeLook(t, "die");
        startWinMoment();   // slow motion, then finishCombatWin (15 §3.8)
        return true;
    }
    const std::string name = cs.foes[(size_t)t].enemy.name;
    foeDown(t);
    cs.log = trParam(locale_.tr("battle.foe_down"), "name", name);
    return false;
}

// Defense bar tapped: doesn't apply any damage itself -- it just banks whatever power the
// auto-moving marker currently reads as a shield (mitigation curve, Perfect Guard, and Guardian's
// flat mitigation floor per docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.2 are all applied later, in
// resolveEnemyClockFire(), when that shield is actually spent). A second tap before the enemy's
// clock fires simply overwrites the banked value -- most recent tap wins, no stacking.
void Game::resolveDefenseTap() {
    toms::PowerBarParams bar = toms::effectiveDefenseBar(equipped_, equipmentDefs_);
    float power = toms::powerFromPosition(bar, cs.defBar.pos);
    cs.shieldBanked = true;
    cs.shieldPower = power;
    cs.log = trParam(locale_.tr("battle.shield_log"), "pct", std::to_string((int)std::lround(power)));
}

// The enemy's own real-time clock fired (see update()): spend whatever shield is currently
// banked (if any) and apply the resulting damage to the player -- an un-shielded hit resolves at
// power=0, i.e. full damage, identical to whiffing the old press-and-hold Defense Bar entirely.
void Game::resolveEnemyClockFire(int foe) {
    CombatState::Foe& f = cs.foes[(size_t)foe];
    const toms::MoveDef& m = moves_.get(f.move);   // its move, shown as its intent (docs/gameplay/13 §6.1, 6.2)
    // a special guard play (docs/gameplay/16 §4.3): the hit waits for it; endSkill comes back here with its result
    if (!m.guardPlay.empty() && f.guardShare < 0.0f && f.volleyHit == 0 && !cs.skill.on)
        if (const toms::SpecialMove* d = specialMoves_.find(m.guardPlay)) { startSkill(*d, foe); return; }
    const bool volley = !m.gapsMs.empty() && m.hits > 1;   // its hits one by one, each with its "!" (16 §4.1)
    if (f.volleyHit == 0) f.attacks++;
    auto afterMove = [this, foe] {   // the next move, then its "attacks" events
        if (!cs.active) return;
        chooseFoeMove(foe);
        toms::EventCheck c;
        c.on = "attacks";
        c.attacks = cs.foes[(size_t)foe].attacks;
        runFoeEvents(foe, c);
    };
    if (m.kind == toms::MoveKind::Guard) {   // its guard is over: no attack
        battleEvent("guardEnd", foe, -1);
        afterMove();
        return;
    }
    const int effDef = effectiveDef();   // equipment, skills (half after a rebirth), attributes
    const int atk = (int)std::lround(f.enemy.atk * f.atkMul * m.damage);
    int incoming = std::max(1, atk - effDef);
    float floorMitigation = toms::talentDefenseMitigationFloor(equipped_.talentId);
    float power = cs.shieldBanked ? cs.shieldPower : 0.0f;
    bool guarded = cs.shieldBanked;
    bool parried = guarded && power >= (float)breakSettings_.parryPower;
    cs.shieldBanked = false; cs.shieldPower = 0.0f;
    if (guard_.mode == "timing" || guard_.mode == "warn") {   // one button: just before the hit (15 §3.2), or while its "!" shows (16 §4.2)
        const int since = cs.guardAtMs < 0 ? -1 : cs.clockMs - cs.guardAtMs;
        const toms::GuardTiming gt = guard_.mode == "warn" ? toms::guardWarn(since, moveWarnMs(m), guard_)
                                                           : toms::guardTiming(since, guard_);
        cs.guardAtMs = -1;   // a press answers one move
        guarded = gt == toms::GuardTiming::Parry || gt == toms::GuardTiming::Block;
        parried = gt == toms::GuardTiming::Parry;
        power = parried ? 100.0f : gt == toms::GuardTiming::Block ? guard_.blockPower : 0.0f;
        if (parried) { popWord(locale_.tr("battle.word_parry"), -1, "parry"); feelMoment("parry"); }
        else if (gt == toms::GuardTiming::Block) { popWord(locale_.tr("battle.word_block"), -1, "block"); feelMoment("block"); }
        else if (gt == toms::GuardTiming::Early) popWord(locale_.tr("battle.word_early"), -1, "early");
    }
    if (m.kind == toms::MoveKind::Heavy && guarded && parried) {
        // a perfect guard parries a heavy blow: no damage, it staggers and takes more (13 §6.2)
        battleEvent("parry", -1, foe, (int)std::lround(power));
        playerLook("parry");
        if (guard_.mode == "bar") feelMoment("parry");
        f.volleyHit = 0;
        startAttackPause();
        popDamage(0, -1, "guard");
        stunFoe(foe, breakSettings_.parryMs, breakSettings_.parryDamage, false);
        if (toms::addBreak(cs.foes[(size_t)foe].breakGauge, (float)breakSettings_.parryBreak, foeBreakPoints(foe)))
            stunFoe(foe, breakSettings_.brokenMs, breakSettings_.brokenDamage, true);
        cs.log = trParam(locale_.tr("battle.parry"), "name", f.enemy.name);
        afterMove();
        return;
    }
    if (m.kind == toms::MoveKind::Spell) power *= 0.5f;   // a guard only halves a spell
    int dmg = 0;
    const int hits = volley ? 1 : m.kind == toms::MoveKind::Jabs ? m.hits : 1;
    for (int k = 0; k < hits; k++) dmg += toms::computeDefenseDamage(incoming, power, floorMitigation);
    if (f.guardShare >= 0.0f) {   // the special guard play's result: what was done takes that much off (16 §4.3)
        dmg = (int)std::lround((float)dmg * (1.0f - std::clamp(f.guardShare, 0.0f, 1.0f)));
        guarded = f.guardShare > 0.0f;
        if (f.guardShare >= 1.0f) {
            popWord(locale_.tr("battle.word_parry"), -1, "parry");
            stunFoe(foe, breakSettings_.parryMs, breakSettings_.parryDamage, false);
        }
        f.guardShare = -1.0f;
    }
    if (m.kind == toms::MoveKind::Jabs) interruptCombo(foe);

    const float hpBefore = playerHpPct();
    cs.playerHP -= dmg;
    cs.damageTaken += dmg;
    if (dmg > 0) feelMoment(m.kind == toms::MoveKind::Heavy || m.kind == toms::MoveKind::Spell ? "heavyHurt" : "hurt");
    battleEvent("hit", foe, -1, dmg, f.move + (guarded ? " guarded " + std::to_string((int)std::lround(power)) : std::string()));
    foeLook(foe, "attack");   // its sound; the lunge lands as the number appears
    startAttackPause();
    popDamage(dmg, -1, "hurt");

    if (cs.playerHP <= 0 && cs.survivalArmed) {
        // a_sanctuary_echo: once per battle, a lethal hit leaves 1 HP instead of ending the fight.
        cs.survivalArmed = false;
        cs.playerHP = 1;
        cs.log = locale_.tr("battle.sanctuary_survive");
    } else if (cs.playerHP <= 0) {
        cs.log = locale_.tr("battle.player_down");
        battleEvent("down", -1, -1);
        finishCombatLose();
    } else if (!attackRunning() || m.kind != toms::MoveKind::Jabs) {
        cs.log = dmg == 0 ? locale_.tr("battle.perfect_guard") : trParam(locale_.tr("battle.guard_hit"), "dmg", std::to_string(dmg));
    }
    if (!cs.active) return;
    if (volley && f.volleyHit + 1 < m.hits) {   // more hits to come: the next after its gap (its "!" before it)
        f.volleyHit++;
        f.volleyMs = m.gapBefore(f.volleyHit);
    } else {
        f.volleyHit = 0;
    }
    {
        toms::EventCheck c;   // every monster hears the player's HP fall
        c.on = "playerHpBelow";
        c.hpPctBefore = hpBefore;
        c.hpPctAfter = playerHpPct();
        for (size_t i = 0; i < cs.foes.size() && cs.active; i++)
            if (!cs.foes[i].down()) runFoeEvents((int)i, c);
    }
    if (cs.active && cs.foes[(size_t)foe].volleyHit == 0) afterMove();   // its whole move is done
}

// Battle v3 (docs/gameplay/07 phase 1): the first tap starts the combo with the charge built so far; the next taps hit the
// points. The combo's end (all hit, the sweeps run out, or two misses) resolves the attack.
void Game::battleTapAttack() {
    if (!cs.active || cs.winPending || (cs.pace == toms::Pace::Cooldown && cs.atkBar.cooling)) return;
    if (battlePlayKind() != toms::PlayKind::Track) return;   // the weapon's own play: by touch (battlePointer)
    if (!cs.combo.running()) {
        if (cs.pace == toms::Pace::Stamina) {   // the stamina pace: starting an attack costs it (docs/gameplay/13 §6.3)
            if (cs.stamina < (float)paceSettings_.attackCost) {
                cs.log = locale_.tr("battle.no_stamina");
                audio.play("deny");
                return;
            }
            cs.stamina -= (float)paceSettings_.attackCost;
        }
        const std::string key = weaponKey();
        cs.combo.start(toms::comboSettings(weaponPlayOf(key), weaponLevel(key)), comboCharge());
        cs.log = locale_.tr("battle.combo_go");
        audio.play("confirm_click");
        return;
    }
    const std::vector<bool> before = cs.combo.pointsHit();
    switch (cs.combo.tap()) {
    case toms::ComboPlay::Tap::Hit:
        audio.play("confirm_click");
        for (size_t i = 0; i < before.size() && i < cs.combo.pointsHit().size(); i++)
            if (!before[i] && cs.combo.pointsHit()[i]) comboHint((int)i, /*hit=*/true);
        break;
    case toms::ComboPlay::Tap::Miss: audio.play("deny"); break;
    case toms::ComboPlay::Tap::Ignored: break;
    }
    if (cs.combo.done()) resolveCombo();
}

void Game::resolveCombo(const toms::ComboResult* played) {
    const toms::ComboResult r = played ? *played : cs.combo.result();
    cs.combo = toms::ComboPlay{};
    cs.chargeMs = 0;
    const bool activeCrit = cs.nextAttackGuaranteedCrit;   // a scripted perfect release: every point counts
    const float power = activeCrit ? 100.0f : r.power();
    const float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    const int t = cs.target;   // the player's attacks go to the target
    const CombatState::Foe& foe = cs.foes[(size_t)t];
    const toms::MoveDef& fm = moves_.get(foe.move);
    const int baseHit = std::max(1, effectiveAtk() - (int)std::lround(foe.enemy.def * foe.defMul));
    int dmg = toms::computeAttackDamage(baseHit, power, maxMult);
    dmg = (int)std::lround(dmg * (1.0f + weaponPlayOf(weaponKey()).chargeDamage * r.charge));   // the charge
    // the result by points matched (weapon_play.json "results", docs/gameplay/13 §4.2): the weapon's, else the default's
    const toms::WeaponPlay& wp = weaponPlayOf(weaponKey());
    // (a scripted perfect release counts every point)
    const toms::WeaponResult* res = toms::weaponResultFor(wp.results.empty() ? weaponPlay_.get("default") : wp,
                                                          activeCrit ? r.points : r.hits, r.points, activeCrit ? false : r.failed);
    if (res) dmg = (int)std::lround(dmg * res->damage);
    // the answers to its move (13 §6.2): a guard takes most of a hit unless a full charge breaks it; a spell breaks
    // when enough points match; a stunned monster takes more
    bool guardBroken = false;
    if (fm.kind == toms::MoveKind::Guard && foe.staggerMs <= 0) {
        if (r.charge >= 0.99f && !r.failed && r.hits > 0) guardBroken = true;
        else dmg = (int)std::lround(dmg * fm.reduce);
    }
    const bool spellBroken = fm.kind == toms::MoveKind::Spell && !r.failed && r.hits >= fm.breakHits;
    if (foe.staggerMs > 0) dmg = (int)std::lround(dmg * foe.staggerDamage);
    const bool guardedHit = fm.kind == toms::MoveKind::Guard && !guardBroken && foe.staggerMs <= 0;
    if (!activeCrit && r.failed && toms::talentZerosRedZoneAttacks(equipped_.talentId)) dmg = 0;   // Berserker: a fail deals nothing
    if (activeCrit) {
        dmg = (int)std::lround(dmg * cs.nextAttackCritDamageMult);
        cs.nextAttackGuaranteedCrit = false;
        cs.nextAttackCritDamageMult = 1.0f;
    }
    // the revamp (docs/gameplay/15 §3.3, §3.4): the result's tier, the perfect chain, the ultimate
    const bool ult = cs.ultimate;
    cs.ultimate = false;
    std::string tier = r.failed || r.hits == 0 ? "miss"
                     : res && (res->name == "perfect" || res->name == "great") ? res->name : "hit";
    if (r.perfects >= 0 && tier != "miss")   // a touch play (16 §3): perfect when all hit and half were perfect
        tier = r.hits >= r.points && r.perfects * 2 >= r.points ? "perfect" : r.hits >= r.points || r.hits * 5 >= r.points * 3 ? "great" : "hit";
    if (tier == "perfect") cs.chain = std::min(feel_.chainMax, cs.chain + 1);
    else if (tier == "miss") cs.chain = 0;
    cs.bestChain = std::max(cs.bestChain, cs.chain);
    if (tier == "perfect") cs.perfects++;
    cs.attacksDone++;
    if (cs.chain > 1) dmg = (int)std::lround(dmg * (1.0f + feel_.chainStep * (float)(cs.chain - 1)));
    if (ult) dmg = (int)std::lround(dmg * (foe.staggerMs > 0 ? 4.0f : 2.0f));   // ×2, ×4 in a break
    battleEvent("combo", -1, t, r.hits, std::to_string(r.hits) + "/" + std::to_string(r.points) + (r.failed ? " failed" : "") +
                                          (res && !res->name.empty() ? " " + res->name : std::string()));
    const int dealt = cs.playDealt;   // the play's answers already hit (16 §3): this is the rest of the attack
    cs.playDealt = 0;
    if (dealt > 0) dmg = std::max(0, dmg - dealt);
    battleEvent("hit", -1, t, dmg);
    lastAttackDamage_ = r.failed || r.hits == 0 ? 0 : std::max(dmg, dealt);   // the battle rig: attack, or miss for a fail
    if (dmg > 0 && !r.failed) cs.superCharge = std::min(run_.superMax(), cs.superCharge + 1);
    if (res && !res->sound.empty()) audio.play(res->sound);
    else playerLook("attack");
    startAttackPause();
    feelMoment(ult ? "ultimate" : tier);
    if (dmg > 0 || dealt == 0) popDamage(dmg, t, ult ? "perfect" : tier);
    // its named move (07 §5.4): the result row's "move", over the monster; the chain by the player
    if (res && !res->move.empty() && locale_.has(res->move)) popWord(locale_.tr(res->move), t, "move-" + tier);
    if (cs.chain > 1) popWord(trParam(locale_.tr("battle.word_chain"), "n", std::to_string(cs.chain)), -1, "chain");
    if (lastAttackDamage_ > 0) {   // the weapon's particles for its level (or the result's): off it now, on the monster with the number
        if (const toms::WeaponFx* fx = battleWeaponFx(ult)) {
            playBattleFx(fx->release, -1);
            playBattleFx(res && !res->fx.empty() ? res->fx : fx->hit, t, hitNumberDelayMs_);
        }
    } else if (res && !res->fx.empty()) {
        playBattleFx(res->fx, -1);   // a miss: the result's effect at the player (weapon_play.json "miss_puff")
    }
    if (battleRigReady()) {
        const std::string bigClip = ult ? std::string("attack_big") : res ? res->anim : std::string();   // the ultimate: the big clip
        const bool big = lastAttackDamage_ > 0 && !bigClip.empty() && battleCharacterAnim_ && battleCharacterAnim_->find(bigClip);
        setBattleRigState(lastAttackDamage_ > 0 ? "attack" : "miss", true, big ? bigClip : std::string());
    }
    damageFoe(t, dmg);
    const std::string combo = trParam(trParam(locale_.tr(r.failed ? "battle.combo_fail" : "battle.combo_hits"), "hits",
                                              std::to_string(r.hits)), "n", std::to_string(r.points));
    std::string hitText = trParam(locale_.tr(guardedHit ? "battle.guarding" : "battle.hit"), "dmg", std::to_string(dmg));
    hitText = trParam(hitText, "name", foeLogName(t));
    if (res && !res->name.empty() && locale_.has("battle.result." + res->name))
        hitText = locale_.tr("battle.result." + res->name) + " " + hitText;
    if (afterPlayerHit(combo + " " + hitText, combo + " " + trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg))))
        return;
    if (!cs.foes[(size_t)t].down()) {
        CombatState::Foe& f = cs.foes[(size_t)t];
        std::string extra;
        if (guardBroken) {
            extra = trParam(locale_.tr("battle.guard_broken"), "name", f.enemy.name);
            battleEvent("guardBreak", -1, t);
            f.clockMs = 0;
            chooseFoeMove(t);
        } else if (spellBroken) {
            extra = trParam(locale_.tr("battle.spell_broken"), "name", f.enemy.name);
            battleEvent("spellBreak", -1, t);
            f.clockMs = 0;
            chooseFoeMove(t);
        }
        if (res) {
            if (res->stunMs > 0) stunFoe(t, res->stunMs, 1.0f, false);
            if (res->pushMs > 0) cs.foes[(size_t)t].clockMs -= res->pushMs;
        }
        // the break gauge (13 §6.4): matched points fill it; full, the monster is stunned and takes more
        const float add = r.failed ? 0.0f : (float)r.hits + (res ? res->breakAdd : 0.0f) +
                                                (guardBroken ? (float)breakSettings_.guardBreak : 0.0f);
        if (cs.foes[(size_t)t].staggerMs <= 0 || !cs.foes[(size_t)t].broken)
            if (toms::addBreak(cs.foes[(size_t)t].breakGauge, add, foeBreakPoints(t))) {
                stunFoe(t, breakSettings_.brokenMs, breakSettings_.brokenDamage, true);
                extra = trParam(locale_.tr("battle.break"), "name", cs.foes[(size_t)t].enemy.name);
            }
        if (r.hits == r.points && !r.failed) {
            toms::EventCheck c;
            c.on = "perfectHit";
            runFoeEvents(t, c);
        }
        if (!extra.empty() && cs.active) cs.log += " " + extra;
    }
    if (!cs.active) return;
    if (cs.pace == toms::Pace::Cooldown) {
        cs.atkBar.cooling = true;
        cs.atkBar.cooldownMs = barCooldownMs();   // AGI shortens it
    }
}

void Game::battleTapDefense() {
    if (!cs.active || cs.winPending || cs.skill.on) return;
    if (guard_.mode == "warn") {   // only while an "!" shows over a monster (16 §4.2); it stops the player's attack
        if (cs.guardLockMs > 0 || cs.guardAtMs >= 0) return;
        stopOwnAttack();
        bool warned = false;
        for (size_t i = 0; i < cs.foes.size(); i++) warned = warned || foeWarning((int)i);
        if (!warned) {   // no "!": too early; the guard locks a moment
            popWord(locale_.tr("battle.word_early"), -1, "early");
            cs.guardLockMs = guard_.missLockMs;
            audio.play("deny");
            return;
        }
        if (cs.pace == toms::Pace::Stamina) {
            if (cs.stamina < (float)paceSettings_.guardCost) { cs.log = locale_.tr("battle.no_stamina"); audio.play("deny"); return; }
            cs.stamina -= (float)paceSettings_.guardCost;
        }
        cs.guardAtMs = cs.clockMs;
        audio.play("confirm_click");
        return;
    }
    if (guard_.mode == "timing") {   // one button (docs/gameplay/15 §3.2): it answers the next hit, judged when it lands
        if (cs.guardCdMs > 0) return;
        if (cs.pace == toms::Pace::Stamina) {
            if (cs.stamina < (float)paceSettings_.guardCost) { cs.log = locale_.tr("battle.no_stamina"); audio.play("deny"); return; }
            cs.stamina -= (float)paceSettings_.guardCost;
        }
        cs.guardAtMs = cs.clockMs;
        cs.guardCdMs = guard_.cooldownMs;
        audio.play("confirm_click");
        return;
    }
    if (cs.pace == toms::Pace::Stamina) {   // each guard tap costs stamina, no wait (docs/gameplay/13 §6.3)
        if (cs.stamina < (float)paceSettings_.guardCost) {
            cs.log = locale_.tr("battle.no_stamina");
            audio.play("deny");
            return;
        }
        cs.stamina -= (float)paceSettings_.guardCost;
        resolveDefenseTap();
        return;
    }
    if (cs.defBar.cooling) return;
    resolveDefenseTap();
    cs.defBar.cooling = true;
    cs.defBar.cooldownMs = barCooldownMs();
}

// Super Attack: a guaranteed, no-timing-required strong hit once the gauge is full (§4 of
// docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md) -- resolved at power=100 against the Attack Bar's own
// (equipment-tuned) maxMult ceiling, the same number a manual Perfect release already reaches.
// Independent of the Attack bar's own cooldown; doesn't touch it.
// The ultimate (docs/gameplay/15 §3.4; 14's 絕招): when the super charge is full, the weapon's finisher. A cut-in holds the
// fight a moment, then a harder play starts at once with a full charge (two more points); its hit does ×2, ×4 in a
// break, with the weapon's best effects and its big clip (resolveCombo).
void Game::battleTapSuper() {
    if (!cs.active || cs.winPending || cs.superCharge < run_.superMax() || cs.combo.running() || cs.skill.on) return;
    cs.superCharge = 0;
    cs.ultimate = true;
    const std::string key = weaponKey();
    if (const toms::SpecialMove* d = specialMoves_.forWeapon(key, "ultimate")) {   // the weapon's own move (16 §6): it cuts in
        const std::string s = feel_.sound("ultimate");
        if (!s.empty()) audio.play(s);
        startSkill(*d, -1);
        return;
    }
    const std::string name = equipmentDefs_.count(key) ? itemName(key) : locale_.tr("battle.unarmed");
    showBanner(trParam(locale_.tr("battle.word_ultimate"), "name", name), "ultimate", 900);
    cs.hitStopMs = std::max(cs.hitStopMs, settings_.reduceEffects ? 200 : 600);   // the cut-in holds the fight
    const std::string s = feel_.sound("ultimate");
    if (!s.empty()) audio.play(s);
    toms::ComboSettings cset = toms::comboSettings(weaponPlayOf(key), weaponLevel(key));
    cset.points = std::min(toms::kMaxComboPoints, cset.points + 2);
    cs.atkBar.cooling = false;
    if (battlePlayKind() != toms::PlayKind::Track) {   // the weapon's own play, harder (it cuts in over the normal one)
        cs.touch.stop();
        cs.touchPaid = false;
        cs.playDealt = 0;
        armTouchPlay(true, cset.points);
    } else {
        cs.combo.start(cset, 1.0f);
    }
    battleEvent("super", -1, cs.target, cset.points, "ultimate");
    cs.log = locale_.tr("battle.combo_go");
}

// (each weapon's own play, skills and the training ground: game_skills.cpp)

// Equipment actives (2026-09-17, rewired onto equipment_actives.h -- see CombatState's own
// comment for why): a no-op unless the currently equipped gear grants an active
// (equippedActives()) and toms::canUseActive() says this battle still has a use of it, off
// cooldown, with no st_silence in effect. Only one active fires at a time -- the first slice's
// single on-screen button -- matching ids[0], same as before this rewire. "guaranteedCrit" arms
// a flag consumed by the next resolveAttackTap() (a buff on the next attack, not an attack in its
// own right, per a_qingxiao_edge's "下一次攻擊..." description); "surviveLethal" arms a flag
// consumed by the next resolveEnemyClockFire() that would otherwise end the fight.
// ActiveDefinition carries no display name yet (a real, still-open content gap) -- the log falls
// back to the raw id rather than inventing a name lookup this rewire doesn't own.
void Game::battleTapActive() {
    if (!cs.active) return;
    auto ids = toms::equippedActives(equipped_, equipmentDefs_);
    if (ids.empty()) return;
    const std::string& id = ids[0];
    if (!toms::canUseActive(id, activeDefs_, cs.activeRuntime, cs.activeStatus)) return;
    toms::ActiveEffect eff = toms::useActive(id, activeDefs_, cs.activeRuntime, cs.activeStatus);
    if (!eff.fired) return;
    if (eff.guaranteedCrit) {
        cs.nextAttackGuaranteedCrit = true;
        cs.nextAttackCritDamageMult = eff.damageMultiplier;
    }
    if (eff.surviveLethal) cs.survivalArmed = true;
    cs.log = trParam(locale_.tr("battle.active_armed"), "name", id);
    battleEvent("active", -1, -1, 0, id);
    audio.play("confirm_click");
}

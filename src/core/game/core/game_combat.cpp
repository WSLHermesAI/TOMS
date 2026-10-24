// game_combat.cpp — battle logic: start/finish, and Battle v2's action resolution
// (tap-to-freeze attack/defense bars, the enemy clock, super attack). Split out of game.cpp 2026-09-13.
#include "game_internal.h"

#include <limits>

using namespace toms::game_detail;

void Game::startCombat(const EnemyInst& e) {
    cs.enemy = e; cs.playerHP = pl.hp; cs.enemyHP = e.hp; cs.active = true; cs.won = false;
    cs.atkBar = CombatState::AutoBar{};
    cs.defBar = CombatState::AutoBar{};
    cs.combo = toms::ComboPlay{};
    cs.chargeMs = 0;
    cs.shieldBanked = false; cs.shieldPower = 0.0f;
    cs.enemyClockMs = 0;
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
    monsterRig_.stop();      // the new monster starts idle (updateMonsterRig)
    monsterRigState_.clear();
    monsterRigClip_.clear();
    monsterRigRequest_.clear();
    cs.log = locale_.tr("battle.start");
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

void Game::setBattleRigState(const std::string& state, bool restart) {
    if (state == battleRigState_ && !restart) return;
    battleRigState_ = state;
    // The weapon's clip for this status (items.json "battleAnim" "clips"; docs/gameplay/07 phase 2), else the status's own name.
    auto def = equipmentDefs_.find(equipped_.weaponId);
    const std::string clipName = def != equipmentDefs_.end() ? def->second.battleClip(state) : state;
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
    backdrop.tint[3] = 0xcc / 255.0f;
    if (ren) ren->drawSprite(backdrop);
    const float size = 134.0f;
    // The player's place on the battle screen (battle.rcss #player: 262, 84, 134 x 134): the feet at its bottom centre,
    // the 32-pixel character filling its height.
    if (battleRigReady() && !battleRigState_.empty())
        battleRig_.draw(ren, battleCharacterAnim_ ? battleCharacterAnim_->atlases() : kNone,
                        battleHeldAnim_ ? battleHeldAnim_->atlases() : kNone,
                        toms::anim::placement(262.0f + size / 2.0f, 84.0f + size, size / 32.0f));
    // The monster's (#enemy: 652, 84, 134 x 134): its sprite's longer side fills the box (a 2-wide golem as wide as the
    // box), feet at the bottom centre, as the picture it replaces.
    if (monsterRigReady() && !monsterRigState_.empty()) {
        const toms::AtlasRegion* r = spriteAtlas_.find(monsterSprite());
        const float longer = r ? (float)std::max(std::max(r->origW, r->origH), 1) : 32.0f;
        monsterRig_.draw(ren, monsterAnim_->atlases(), kNone, toms::anim::placement(652.0f + size / 2.0f, 84.0f + size, size / longer));
    }
    if (battleFxFile_) {
        for (BattleFx& b : battleFx_)
            if (!b.overUi) b.fx.draw(ren, battleFxFile_->atlases(), glm::mat3(1.0f), nullptr);
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

// One-shot, at the player's weapon (#player's box, a little ahead of the character) or the monster's middle (#enemy's).
void Game::playBattleFx(const std::string& effect, bool atEnemy, int delayMs) {
    if (atEnemy) playBattleFxAt(effect, 719.0f, 151.0f, false, delayMs);
    else playBattleFxAt(effect, 360.0f, 150.0f, false, delayMs);
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
        b.fx.play(e);
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

// ---- the monster on the battle screen (Game::monsterRigState) ----

std::string Game::monsterSprite() const {
    return cs.enemy.boss ? std::string("boss_demonlord") : typeSprite("monster", cs.enemy.id);
}

void Game::setMonsterRigState(const std::string& state) {
    monsterRigState_ = state;
    // the monster's own clip for this status, else the shared one
    const toms::anim::Clip* c = monsterAnim_->find(state + "_" + cs.enemy.id);
    if (!c) c = monsterAnim_->find(state);
    monsterRigClip_ = c ? c->name : std::string();
    if (!c) { monsterRig_.stop(); return; }
    monsterFitted_ = fitClipToCharacter(*c, monsterSprite());
    monsterRig_.play(&monsterFitted_, nullptr);
}

void Game::updateMonsterRig(int dtMs) {
    const bool shown = cs.active || cs.won || cs.resultPauseMs > 0;
    if (!shown) {
        if (!monsterRigState_.empty()) { monsterRig_.stop(); monsterRigState_.clear(); monsterRigClip_.clear(); }
        monsterRigRequest_.clear();
        return;
    }
    if (!monsterAnim_ && !monsterAnimTried_ && res_) {   // once: without the file the UI's picture stays
        monsterAnimTried_ = true;
        auto a = res_->get<toms::res::AnimRes>(kMonsterBattleAnim);
        if (a->ok()) monsterAnim_ = a;
        else std::fprintf(stderr, "[battle] %s: %s\n", kMonsterBattleAnim, a->error().c_str());
    }
    if (!monsterRigReady()) return;
    if (!monsterRigRequest_.empty() && monsterRigState_ != "die") {   // an attack or a hit: from the start
        setMonsterRigState(monsterRigRequest_);
        monsterRigRequest_.clear();
    } else {
        monsterRigRequest_.clear();
        // attack, hurt and die play to their end; then the fight decides: won = die (it stays gone), else idle
        const bool oneShot = monsterRigState_ == "attack" || monsterRigState_ == "hurt" || monsterRigState_ == "die";
        if (monsterRigState_ != "die" && !(oneShot && !monsterRig_.finished())) {
            const std::string next = cs.won ? "die" : "idle";
            if (next != monsterRigState_) setMonsterRigState(next);
        }
    }
    monsterRig_.update(dtMs);
    monsterRig_.takeEvents();   // (hit: the battle's own timers place the numbers)
}

// The owner's rule (docs/gameplay/07_GAMEPLAY_PLAN.md §4): an attack pauses both sides' clocks until it is over.
void Game::startAttackPause() { cs.attackPauseMs = std::max(cs.attackPauseMs, attackPauseMs_); }

// The number appears when the swing lands, not when the button is pressed: a safe timer owned by this battle.
void Game::popDamage(int dmg, bool overEnemy, int kind) {
    timers_.after(battleTimers_, hitNumberDelayMs_, [this, dmg, overEnemy, kind] {
        CombatState::Popup p;
        p.text = dmg > 0 ? std::to_string(dmg) : locale_.tr("battle.popup_guard");
        p.overEnemy = overEnemy;
        p.kind = dmg > 0 ? kind : 2;
        p.rise = toms::Tween(0.0f, 1.0f, 900, toms::Ease::OutCubic);
        cs.popups.push_back(p);
        if (overEnemy && dmg > 0) monsterRigRequest_ = "hurt";   // the monster flinches as the number appears
    });
}

// Shared win/lose resolution -- identical to the pre-Milestone-6 auto-combat's own win/lose
// handling (rewards, level-up, boss warp, respawn), just extracted so resolveAttackTap/
// battleTapSuper (win) and resolveEnemyClockFire (lose) can all reach it without duplicating it.
void Game::finishCombatWin() {
    if (arenaEndFight(true)) return;   // the weapon test arena: no rewards, the next fight follows
    cs.active = false; cs.won = true;
    pl.hp = cs.playerHP;
    pl.gold += (int)std::lround(cs.enemy.gold * (1.0f + attrEffect("goldGain") / 100.0f));   // LUK
    gainExp(cs.enemy.exp);   // level-ups by data/stats.json
    // Monster cards (docs/gameplay/07_GAMEPLAY_PLAN.md §3): the win is counted, and the card may drop (enemies.json "card").
    if (auto t = enemyTpl.find(cs.enemy.id); t != enemyTpl.end()) {
        const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(cardRng_);
        if (toms::recordCardWin(meta_.cards, cs.enemy.id, toms::cardChanceFromJson(t->second), roll)) {
            const int n = meta_.cards[cs.enemy.id].copies;
            notifications_.push_back({n == 1 ? trParam(locale_.tr("cards.new"), "name", cs.enemy.name)
                                             : trParam(trParam(locale_.tr("cards.more"), "name", cs.enemy.name), "n", std::to_string(n)), 3500});
            newsCards_ = true;
        }
    }
    // remove monster entity from stage -- only when it stands on the map (a test scenario's enemy does not: x, y -1)
    const bool onMap = cs.enemy.y >= 0 && cs.enemy.y < (int)st.tiles.size() &&
                       cs.enemy.x >= 0 && cs.enemy.x < (int)st.tiles[cs.enemy.y].size();
    if (onMap) {
        for (auto& e : st.entities) if (e.x==cs.enemy.x && e.y==cs.enemy.y && e.id==cs.enemy.id) { e.consumed=true; fireLinkedEvents(e); }
        st.tiles[cs.enemy.y][cs.enemy.x] = '.';
        // Milestone 7: persist the clear so it survives a reload of this floor (stairs or the Stage
        // Select hub) -- see entityStatus_'s declaration in game.h.
        toms::setEntityStatus(entityStatus_, statusKeyAt(cs.enemy.x, cs.enemy.y), toms::EntityStatus::Defeated);
    }
    {   // the weapon's win (docs/gameplay/07 phase 1): enough of them raise its level -- more points, bigger bonuses
        const std::string key = weaponKey();
        const int before = weaponLevel(key);
        weaponUses_[key]++;
        const int after = weaponLevel(key);
        if (after > before) {
            const std::string name = equipmentDefs_.count(key) ? itemName(key) : locale_.tr("battle.unarmed");
            const int points = toms::weaponLevelEntry(weaponPlayOf(key), after).points;
            notifications_.push_back({trParam(trParam(trParam(locale_.tr("battle.weapon_level_up"), "name", name), "lv",
                                                      std::to_string(after)), "n", std::to_string(points)), 3500});
        }
    }
    toms::globalEventBus().publish(toms::EnemyDefeated{cs.enemy.id, curStage});
    if (cs.enemy.boss) { cs.log = locale_.tr("battle.boss_win"); loadStage("stage_11"); }
    markProgressDirty();   // gold/exp/level/entity-status all changed: autosave will flush it
}

void Game::finishCombatLose() {
    if (arenaEndFight(false)) return;   // the weapon test arena: no death counted, no respawn
    cs.active = false; cs.won = false;
    // M7 (first slice): noteDeath() existed since S3 (schemaVersion 3's deathsTotal/deathsNonBoss)
    // but nothing ever actually called it -- a real, pre-existing gap found while scoping this
    // slice, since e_13's condition depends entirely on this counter being real.
    run_.noteDeath(cs.enemy.boss);
    toms::recordCardLoss(meta_.cards, cs.enemy.id);   // the monster's card record counts it
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
    int baseHit = std::max(1, effAtk - cs.enemy.def);
    int dmg = toms::computeAttackDamage(baseHit, power, maxMult);
    if (!activeCrit && toms::zoneFromPosition(bar, pos) == toms::PowerZone::Red && toms::talentZerosRedZoneAttacks(equipped_.talentId))
        dmg = 0;   // Berserker: a high ceiling, but Red-zone releases deal nothing
    if (activeCrit) {
        dmg = (int)std::lround(dmg * cs.nextAttackCritDamageMult);
        cs.nextAttackGuaranteedCrit = false;
        cs.nextAttackCritDamageMult = 1.0f;
    }

    cs.enemyHP -= dmg;
    lastAttackDamage_ = dmg;   // the battle rig shows attack or miss
    if (dmg > 0) cs.superCharge = std::min(run_.superMax(), cs.superCharge + 1);   // M8: superMax is per-run, halved by rebirth()
    audio.play("player_attack");
    startAttackPause();
    popDamage(dmg, /*overEnemy=*/true, 0);

    if (cs.enemyHP <= 0) {
        cs.log = trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg));
        finishCombatWin();
    } else {
        cs.log = trParam(locale_.tr("battle.hit"), "dmg", std::to_string(dmg));
    }
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
void Game::resolveEnemyClockFire() {
    const int effDef = effectiveDef();   // equipment, skills (half after a rebirth), attributes
    int incoming = std::max(1, cs.enemy.atk - effDef);
    float floorMitigation = toms::talentDefenseMitigationFloor(equipped_.talentId);
    float power = cs.shieldBanked ? cs.shieldPower : 0.0f;
    int dmg = toms::computeDefenseDamage(incoming, power, floorMitigation);
    cs.shieldBanked = false; cs.shieldPower = 0.0f;

    cs.playerHP -= dmg;
    if (dmg > 0) audio.play("enemy_attack");
    monsterRigRequest_ = "attack";   // the lunge lands as the number appears (monster_battle.anim)
    startAttackPause();
    popDamage(dmg, /*overEnemy=*/false, 0);

    if (cs.playerHP <= 0 && cs.survivalArmed) {
        // a_sanctuary_echo: once per battle, a lethal hit leaves 1 HP instead of ending the fight.
        cs.survivalArmed = false;
        cs.playerHP = 1;
        cs.log = locale_.tr("battle.sanctuary_survive");
    } else if (cs.playerHP <= 0) {
        cs.log = locale_.tr("battle.player_down");
        finishCombatLose();
    } else {
        cs.log = dmg == 0 ? locale_.tr("battle.perfect_guard") : trParam(locale_.tr("battle.guard_hit"), "dmg", std::to_string(dmg));
    }
}

// Battle v3 (docs/gameplay/07 phase 1): the first tap starts the combo with the charge built so far; the next taps hit the
// points. The combo's end (all hit, the sweeps run out, or two misses) resolves the attack.
void Game::battleTapAttack() {
    if (!cs.active || cs.atkBar.cooling) return;
    if (!cs.combo.running()) {
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

void Game::resolveCombo() {
    const toms::ComboResult r = cs.combo.result();
    cs.combo = toms::ComboPlay{};
    cs.chargeMs = 0;
    const bool activeCrit = cs.nextAttackGuaranteedCrit;   // a scripted perfect release: every point counts
    const float power = activeCrit ? 100.0f : r.power();
    const float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    const int baseHit = std::max(1, effectiveAtk() - cs.enemy.def);
    int dmg = toms::computeAttackDamage(baseHit, power, maxMult);
    dmg = (int)std::lround(dmg * (1.0f + weaponPlayOf(weaponKey()).chargeDamage * r.charge));   // the charge
    if (!activeCrit && r.failed && toms::talentZerosRedZoneAttacks(equipped_.talentId)) dmg = 0;   // Berserker: a fail deals nothing
    if (activeCrit) {
        dmg = (int)std::lround(dmg * cs.nextAttackCritDamageMult);
        cs.nextAttackGuaranteedCrit = false;
        cs.nextAttackCritDamageMult = 1.0f;
    }
    cs.enemyHP -= dmg;
    lastAttackDamage_ = r.failed || r.hits == 0 ? 0 : dmg;   // the battle rig: attack, or miss for a fail
    if (dmg > 0 && !r.failed) cs.superCharge = std::min(run_.superMax(), cs.superCharge + 1);
    audio.play("player_attack");
    startAttackPause();
    popDamage(dmg, /*overEnemy=*/true, r.hits == r.points && !r.failed ? 1 : 0);
    if (lastAttackDamage_ > 0)   // the weapon's particles for its level: off it now, on the monster with the number
        if (const toms::WeaponFx* fx = battleWeaponFx()) {
            playBattleFx(fx->release, /*atEnemy=*/false);
            playBattleFx(fx->hit, /*atEnemy=*/true, hitNumberDelayMs_);
        }
    if (battleRigReady()) setBattleRigState(lastAttackDamage_ > 0 ? "attack" : "miss", true);
    const std::string combo = trParam(trParam(locale_.tr(r.failed ? "battle.combo_fail" : "battle.combo_hits"), "hits",
                                              std::to_string(r.hits)), "n", std::to_string(r.points));
    if (cs.enemyHP <= 0) {
        cs.log = combo + " " + trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg));
        finishCombatWin();
        return;
    }
    cs.log = combo + " " + trParam(locale_.tr("battle.hit"), "dmg", std::to_string(dmg));
    cs.atkBar.cooling = true;
    cs.atkBar.cooldownMs = barCooldownMs();   // AGI shortens it
}

void Game::battleTapDefense() {
    if (!cs.active || cs.defBar.cooling) return;
    resolveDefenseTap();
    cs.defBar.cooling = true;
    cs.defBar.cooldownMs = barCooldownMs();
}

// Super Attack: a guaranteed, no-timing-required strong hit once the gauge is full (§4 of
// docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md) -- resolved at power=100 against the Attack Bar's own
// (equipment-tuned) maxMult ceiling, the same number a manual Perfect release already reaches.
// Independent of the Attack bar's own cooldown; doesn't touch it.
void Game::battleTapSuper() {
    if (!cs.active || cs.superCharge < run_.superMax()) return;
    float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    int baseHit = std::max(1, effectiveAtk() - cs.enemy.def);
    int dmg = toms::computeAttackDamage(baseHit, 100.0f, maxMult);
    cs.enemyHP -= dmg;
    cs.superCharge = 0;
    audio.play("player_attack");
    startAttackPause();
    popDamage(dmg, /*overEnemy=*/true, 1);
    if (const toms::WeaponFx* fx = battleWeaponFx(/*top=*/true)) {   // the super attack: the weapon's best effects
        playBattleFx(fx->release, /*atEnemy=*/false);
        playBattleFx(fx->hit, /*atEnemy=*/true, hitNumberDelayMs_);
    }

    if (cs.enemyHP <= 0) {
        cs.log = trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg));
        finishCombatWin();
    } else {
        cs.log = trParam(locale_.tr("battle.super_hit"), "dmg", std::to_string(dmg));
    }
}

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
    audio.play("confirm_click");
}

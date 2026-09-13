// game_combat.cpp — battle logic: start/finish, and Battle v2's action resolution
// (tap-to-freeze attack/defense bars, the enemy clock, super attack). Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::startCombat(const EnemyInst& e) {
    cs.enemy = e; cs.playerHP = pl.hp; cs.enemyHP = e.hp; cs.active = true; cs.won = false;
    cs.atkBar = CombatState::AutoBar{};
    cs.defBar = CombatState::AutoBar{};
    cs.shieldBanked = false; cs.shieldPower = 0.0f;
    cs.enemyClockMs = 0;
    cs.superCharge = 0;
    cs.resultPauseMs = 0;
    cs.log = locale_.tr("battle.start");
}

// Shared win/lose resolution -- identical to the pre-Milestone-6 auto-combat's own win/lose
// handling (rewards, level-up, boss warp, respawn), just extracted so resolveAttackTap/
// battleTapSuper (win) and resolveEnemyClockFire (lose) can all reach it without duplicating it.
void Game::finishCombatWin() {
    cs.active = false; cs.won = true;
    pl.hp = cs.playerHP;
    pl.gold += cs.enemy.gold; pl.exp += cs.enemy.exp;
    int need = pl.lv * 30;
    while (pl.exp >= need) {
        pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30;
        pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
    }
    // remove monster entity from stage
    for (auto& e : st.entities) if (e.x==cs.enemy.x && e.y==cs.enemy.y && e.id==cs.enemy.id) e.consumed=true;
    st.tiles[cs.enemy.y][cs.enemy.x] = '.';
    // Milestone 7: persist the clear so it survives a reload of this floor (stairs or the Stage
    // Select hub) -- see entityStatus_'s declaration in game.h.
    toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, cs.enemy.x, cs.enemy.y), toms::EntityStatus::Defeated);
    toms::globalEventBus().publish(toms::EnemyDefeated{cs.enemy.id, curStage});
    if (cs.enemy.boss) { cs.log = locale_.tr("battle.boss_win"); loadStage("stage_11"); }
    markProgressDirty();   // gold/exp/level/entity-status all changed: autosave will flush it
}

void Game::finishCombatLose() {
    cs.active = false; cs.won = false;
    cs.resultPauseMs = 300;   // keep the "you fell" message on screen briefly (see CombatState)
    pl.hp = pl.maxhp/2; // respawn at stage start
    loadStage(curStage); // reset monsters/items
    markProgressDirty();
}

// Attack bar tapped: apply damage to the enemy from wherever its auto-moving marker currently is
// (FIGHT_SCENE_DESIGN.md §4's power_mult curve, with the equipped weapon's maxMult and
// Berserker's Red-zone-deals-0 rule per MAIN_BATTLE_SCENE_DESIGN.md §4.2). A kill ends the fight
// immediately -- there's no "the enemy still gets to retaliate this round" any more, since the
// enemy's own clock is what decides when it attacks, independent of this tap.
void Game::resolveAttackTap() {
    toms::PowerBarParams bar = toms::effectiveAttackBar(equipped_, equipmentDefs_);
    float pos = cs.atkBar.pos;
    float power = toms::powerFromPosition(bar, pos);
    float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    int effAtk = pl.atk, effDef = pl.def;
    toms::applyEquipmentStats(equipped_, equipmentDefs_, effAtk, effDef);
    int baseHit = std::max(1, effAtk - cs.enemy.def);
    int dmg = toms::computeAttackDamage(baseHit, power, maxMult);
    if (toms::zoneFromPosition(bar, pos) == toms::PowerZone::Red && toms::talentZerosRedZoneAttacks(equipped_.talentId))
        dmg = 0;   // Berserker: a high ceiling, but Red-zone releases deal nothing

    cs.enemyHP -= dmg;
    if (dmg > 0) cs.superCharge = std::min(CombatState::kSuperThreshold, cs.superCharge + 1);
    audio.play("player_attack");

    if (cs.enemyHP <= 0) {
        cs.log = trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg));
        finishCombatWin();
    } else {
        cs.log = trParam(locale_.tr("battle.hit"), "dmg", std::to_string(dmg));
    }
}

// Defense bar tapped: doesn't apply any damage itself -- it just banks whatever power the
// auto-moving marker currently reads as a shield (mitigation curve, Perfect Guard, and Guardian's
// flat mitigation floor per MAIN_BATTLE_SCENE_DESIGN.md §4.2 are all applied later, in
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
    int effAtk = pl.atk, effDef = pl.def;
    toms::applyEquipmentStats(equipped_, equipmentDefs_, effAtk, effDef);
    int incoming = std::max(1, cs.enemy.atk - effDef);
    float floorMitigation = toms::talentDefenseMitigationFloor(equipped_.talentId);
    float power = cs.shieldBanked ? cs.shieldPower : 0.0f;
    int dmg = toms::computeDefenseDamage(incoming, power, floorMitigation);
    cs.shieldBanked = false; cs.shieldPower = 0.0f;

    cs.playerHP -= dmg;
    if (dmg > 0) audio.play("enemy_attack");

    if (cs.playerHP <= 0) {
        cs.log = locale_.tr("battle.player_down");
        finishCombatLose();
    } else {
        cs.log = dmg == 0 ? locale_.tr("battle.perfect_guard") : trParam(locale_.tr("battle.guard_hit"), "dmg", std::to_string(dmg));
    }
}

void Game::battleTapAttack() {
    if (!cs.active || cs.atkBar.cooling) return;
    resolveAttackTap();
    if (!cs.active) return;   // the tap just won the fight -- nothing left to cool down
    cs.atkBar.cooling = true;
    cs.atkBar.cooldownMs = CombatState::kBarCooldownMs;
}

void Game::battleTapDefense() {
    if (!cs.active || cs.defBar.cooling) return;
    resolveDefenseTap();
    cs.defBar.cooling = true;
    cs.defBar.cooldownMs = CombatState::kBarCooldownMs;
}

// Super Attack: a guaranteed, no-timing-required strong hit once the gauge is full (§4 of
// docs/BATTLE_SYSTEM_V2_PROPOSALS.md) -- resolved at power=100 against the Attack Bar's own
// (equipment-tuned) maxMult ceiling, the same number a manual Perfect release already reaches.
// Independent of the Attack bar's own cooldown; doesn't touch it.
void Game::battleTapSuper() {
    if (!cs.active || cs.superCharge < CombatState::kSuperThreshold) return;
    float maxMult = toms::effectiveMaxMult(equipped_, equipmentDefs_);
    int effAtk = pl.atk, effDef = pl.def;
    toms::applyEquipmentStats(equipped_, equipmentDefs_, effAtk, effDef);
    int baseHit = std::max(1, effAtk - cs.enemy.def);
    int dmg = toms::computeAttackDamage(baseHit, 100.0f, maxMult);
    cs.enemyHP -= dmg;
    cs.superCharge = 0;
    audio.play("player_attack");

    if (cs.enemyHP <= 0) {
        cs.log = trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg));
        finishCombatWin();
    } else {
        cs.log = trParam(locale_.tr("battle.super_hit"), "dmg", std::to_string(dmg));
    }
}

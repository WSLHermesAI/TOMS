// game_skills.cpp -- the plays, skills and the training ground of docs/gameplay/16_SKILLS_AND_PLAYS.md, on the reusable
// blocks (cue_track.h, gesture.h, special_moves.h, touch_play.h):
//   - each weapon's own play (touch_play.h): armed when the attack is ready; the sword's cuts and the staff's spells
//     hit as they are answered (playHit), the play's end resolves the rest through resolveCombo;
//   - the "warn" guard and the monsters' rhythm: the "!" (foeWarning), a guard press stops the player's attack;
//   - skills (special_moves.json): a weapon's ultimate or a monster's special guard play, run as demo -> play -> finish;
//   - the training ground: a card spent for fights in a row with more EXP and weapon wins.
#include "game_internal.h"

#include <ctime>

using namespace toms::game_detail;

// ---- each weapon's own play ----

toms::PlayKind Game::battlePlayKind() const {
    if (!playOverride_.empty() && playOverride_ != "weapon") return toms::playKindFromString(playOverride_);
    return toms::playKindFromString(weaponPlayOf(weaponKey()).play);
}

glm::vec4 Game::touchArea() const {
    const glm::vec4 b = fighterBox(std::clamp(cs.target, 0, std::max(0, (int)cs.foes.size() - 1)));
    // the bow's targets on the monster; the circles from it down into the free space under the fighters; the
    // sword's prompts in that space (TouchPlay::promptAt), clear of the monster
    glm::vec4 a = battlePlayKind() == toms::PlayKind::Bow && !cs.skill.on ? glm::vec4(b.x - 30.0f, b.y - 20.0f, b.z + 60.0f, b.w + 40.0f)
                                                                            : glm::vec4(b.x - 120.0f, b.y - 10.0f, b.z + 240.0f, b.w + 210.0f);
    // On the screen, where a finger can reach it: a monster at the edge (the third of three) would push it past the
    // side; the bottom keeps clear of the GUARD / attack panels and the ultimate's row. Moved in, not shrunk, so the
    // circles keep their room.
    const float w = ren ? (float)ren->width() : 1024.0f, h = ren ? (float)ren->height() : 768.0f;
    const float margin = 12.0f, top = 60.0f, bottom = h - 290.0f;
    a.z = std::min(a.z, w - 2.0f * margin);
    a.w = std::min(a.w, std::max(40.0f, bottom - top));
    a.x = std::clamp(a.x, margin, std::max(margin, w - margin - a.z));
    a.y = std::clamp(a.y, top, std::max(top, bottom - a.w));
    return a;
}

toms::TouchPlaySettings Game::touchSettingsFor(toms::PlayKind kind) const {
    toms::TouchPlaySettings s = touchSettings_;
    if (kind == toms::PlayKind::Bow) {   // STR holds the pull longer, DEX keeps the aim still (16 §3.2)
        s.bowHoldMs += std::max(0, attrValue("str")) * s.bowHoldPerStr;
        s.bowShakePx /= 1.0f + (float)std::max(0, attrValue("dex")) * s.bowShakePerDex;
    }
    return s;
}

void Game::armTouchPlay(bool ultimate, int points) {
    const toms::PlayKind kind = battlePlayKind();
    if (kind == toms::PlayKind::Bow && !ultimate) points = 1;   // a normal attack is one arrow (16 §3.2)
    toms::TouchPlaySettings s = touchSettingsFor(kind);
    if (kind == toms::PlayKind::Slice) {   // the level's shape prompts take the last places (16 §3.1)
        const std::string key = weaponKey();
        const std::vector<std::string> shapes = weaponPlayOf(key).shapeCuesAt(weaponLevel(key));
        s.sliceCues.assign((size_t)points, "way");
        for (size_t k = 0; k < shapes.size() && k < s.sliceCues.size(); k++) s.sliceCues[s.sliceCues.size() - shapes.size() + k] = shapes[k];
    }
    touchArmedArea_ = touchArea();
    cs.touch.start(kind, points, ultimate, touchArmedArea_, (uint32_t)playRng_(), s);   // new places and order each attack
    cs.playDealt = 0;
    cs.touchPaid = ultimate;   // the ultimate costs no stamina and runs at once with a full charge
    cs.touchCharge = ultimate ? 1.0f : 0.0f;
    if (!ultimate && cs.touch.started()) {   // the sword and the staff start by themselves: the attack starts now
        cs.touchPaid = true;
        cs.touchCharge = comboCharge();
        if (cs.pace == toms::Pace::Stamina) cs.stamina = std::max(0.0f, cs.stamina - (float)paceSettings_.attackCost);
    }
    touchHeld_ = false;
}

void Game::battlePointer(int phase, float x, float y) {
    if (!cs.active || cs.winPending || !cs.touch.running()) { touchHeld_ = false; return; }
    if (cs.skill.on && cs.skill.phase != CombatState::SkillRun::Phase::Play) return;   // a move's show: watch it
    if (phase == 0 && !cs.touch.started() && cs.pace == toms::Pace::Stamina && cs.stamina < (float)paceSettings_.attackCost) {
        cs.log = locale_.tr("battle.no_stamina");   // the stamina pace: starting an attack costs it (13 §6.3)
        audio.play("deny");
        return;
    }
    if (phase == 0) touchHeld_ = true;
    else if (!touchHeld_) return;   // a press that began on a button, or before the play
    if (phase == 2) touchHeld_ = false;
    const int index = cs.touch.index(), hits = cs.touch.result().hits;
    const bool wasStarted = cs.touch.started();
    cs.touch.pointer(phase, x, y);
    if (cs.touch.started() && !wasStarted && !cs.touchPaid) {   // the bow's first pull: the attack starts (07 §4)
        cs.touchPaid = true;
        cs.touchCharge = comboCharge();
        if (cs.pace == toms::Pace::Stamina) cs.stamina = std::max(0.0f, cs.stamina - (float)paceSettings_.attackCost);
    }
    touchPlayed(index, hits);
}

void Game::stepTouchPlay(int dtMs) {
    const int index = cs.touch.index(), hits = cs.touch.result().hits;
    cs.touch.update(dtMs);
    touchPlayed(index, hits);
}

void Game::touchPlayed(int indexBefore, int hitsBefore) {
    const toms::PlayKind kind = cs.touch.kind();
    const bool skillPlay = cs.skill.on && cs.skill.phase == CombatState::SkillRun::Phase::Play;
    for (const toms::TouchPlay::Hit& h : cs.touch.takeHits()) {
        if (skillPlay) {   // a move's step: its burst, "2/5", the next part of its name (the skill's "step" show)
            if (!h.ok) { audio.play("deny"); continue; }
            cs.skill.steps++;
            for (const toms::ShowBeat& b : cs.skill.def->step) skillBeat(b, {h.x, h.y});
            audio.play("confirm_click");
        } else if (kind == toms::PlayKind::Slice || kind == toms::PlayKind::Circles) {
            playHit(h);   // each cut, each spell hits at once (16 §3.1, §3.3)
        }
    }
    if (!skillPlay && kind != toms::PlayKind::Slice && kind != toms::PlayKind::Circles && cs.touch.index() > indexBefore)
        audio.play(cs.touch.result().hits > hitsBefore ? "confirm_click" : "deny");
    if (kind == toms::PlayKind::Bow && cs.touch.index() > indexBefore && !settings_.bowTutorialDone) {
        settings_.bowTutorialDone = true;   // the first shot: the tutorial has done its job
        toms::ensureSaveDir(toms::defaultSaveDir());
        toms::saveGameSettings(toms::settingsPath(), settings_);
    }
    if (cs.skill.on) return;   // a move's play ends in updateSkill
    if (!cs.touchPaid) return;
    const bool targetFell = cs.target < (int)cs.foes.size() && cs.foes[(size_t)cs.target].down();
    if (!cs.touch.done() && !targetFell) return;
    cs.touchPaid = false;   // resolved once (the done play stays, its marks fading, until the next is armed)
    cs.touch.stop();
    toms::ComboResult r = cs.touch.result();
    r.charge = kind == toms::PlayKind::Bow ? cs.touch.shotCharge() : cs.touchCharge;   // the bow: its pull's charge
    resolveCombo(&r);
}

int Game::normalHitDamage(int foe) const {
    const CombatState::Foe& f = cs.foes[(size_t)std::clamp(foe, 0, (int)cs.foes.size() - 1)];
    const int baseHit = std::max(1, effectiveAtk() - (int)std::lround(f.enemy.def * f.defMul));
    return toms::computeAttackDamage(baseHit, 100.0f, toms::effectiveMaxMult(equipped_, equipmentDefs_));
}

void Game::playHit(const toms::TouchPlay::Hit& h) {
    if (!h.ok) { audio.play("deny"); return; }
    const int t = cs.target;
    if (t < 0 || t >= (int)cs.foes.size() || cs.foes[(size_t)t].down()) return;
    // half a normal hit shared over the answers; the play's end deals the rest of the attack (resolveCombo)
    const CombatState::Foe& f = cs.foes[(size_t)t];
    int dmg = (int)std::lround((float)normalHitDamage(t) * 0.5f / (float)std::max(1, cs.touch.points()) *
                               (h.perfect ? 1.3f : 1.0f) * f.staggerDamage);
    dmg = std::max(1, dmg);
    cs.playDealt += dmg;
    battleEvent("hit", -1, t, dmg, "answer");
    if (const toms::WeaponFx* fx = battleWeaponFx()) {
        if (cs.touch.kind() == toms::PlayKind::Circles) playBattleFxAt(fx->release, h.x, h.y, true);   // the spell leaves the circle
        playBattleFx(fx->hit, t);
    }
    audio.play("confirm_click");
    feelMoment(h.perfect ? "great" : "hit");
    popDamage(dmg, t, h.perfect ? "great" : "hit");
    damageFoe(t, dmg);
}

void Game::stopOwnAttack() {
    if (!attackRunning()) return;
    cs.combo = toms::ComboPlay{};
    cs.touch.stop();
    cs.touchPaid = false;
    cs.playDealt = 0;
    cs.chargeMs = 0;
    if (cs.pace == toms::Pace::Cooldown) { cs.atkBar.cooling = true; cs.atkBar.cooldownMs = barCooldownMs(); }
    battleEvent("interrupt", -1, -1, 0, "guard");
}

// ---- the monsters' rhythm and the "!" (16 §4) ----

int Game::moveWarnMs(const toms::MoveDef& m) const { return m.warnMs >= 0 ? m.warnMs : guard_.warnMs; }

bool Game::foeWarning(int foe) const {
    if (foe < 0 || foe >= (int)cs.foes.size()) return false;
    const CombatState::Foe& f = cs.foes[(size_t)foe];
    if (!cs.active || f.down() || f.staggerMs > 0) return false;
    const toms::MoveDef& m = moves_.get(f.move);
    if (m.kind == toms::MoveKind::Guard) return false;
    const int warn = moveWarnMs(m);
    if (f.volleyHit > 0) return f.volleyMs <= warn;
    return f.moveMs - f.clockMs <= warn;
}

// ---- skills (16 §6; special_moves.json) ----

glm::vec2 Game::skillCentre() const {
    const glm::vec4 p = playerBox_, f = fighterBox(std::clamp(cs.target, 0, std::max(0, (int)cs.foes.size() - 1)));
    return {(p.x + p.z * 0.5f + f.x + f.z * 0.5f) * 0.5f, std::max(p.y + p.w, f.y + f.w) - 10.0f};
}

std::string Game::skillName() const {
    if (!cs.skill.def) return std::string();
    const std::string& k = cs.skill.def->name;
    return locale_.has(k) ? locale_.tr(k) : k;
}

void Game::startSkill(const toms::SpecialMove& def, int owner) {
    stopOwnAttack();
    cs.skill = CombatState::SkillRun{};
    cs.skill.on = true;
    cs.skill.def = &def;
    cs.skill.owner = owner;
    battleEvent("skill", owner, cs.target, 0, def.id);
    for (const toms::ShowBeat* b : toms::beatsDue(def.demo, -1, 0)) skillBeat(*b, skillCentre());
}

void Game::skillBeat(const toms::ShowBeat& b, glm::vec2 at) {
    using P = CombatState::SkillRun::Phase;
    const toms::SpecialMove& d = *cs.skill.def;
    const int t = cs.skill.owner >= 0 ? cs.skill.owner : cs.target;
    if (b.what == "pathFx") {   // particles along the shape in its drawing order, a bigger burst at each corner
        const std::string shape = b.shape.empty() ? d.play.shape : b.shape;
        const glm::vec2 c = skillCentre();
        const int n = std::max(8, b.ms / 60);
        for (int k = 0; k < n; k++) {
            const float f = (float)k / (float)(n - 1);
            const glm::vec2 p = toms::shapePoint(shape, c, d.play.size, f);
            playBattleFxAt(b.fx, p.x, p.y, true, (int)std::lround(f * (float)b.ms));
        }
        const std::vector<glm::vec2> corners = toms::shapeCorners(shape);
        float total = 0.0f, done = 0.0f;
        for (size_t k = 1; k < corners.size(); k++) total += glm::length(corners[k] - corners[k - 1]);
        for (size_t k = 1; k < corners.size() && total > 0.0f; k++) {
            done += glm::length(corners[k] - corners[k - 1]);
            const glm::vec2 p = c + (corners[k] - glm::vec2(0.5f)) * d.play.size;
            playBattleFxAt(b.corner, p.x, p.y, true, (int)std::lround(done / total * (float)b.ms));
        }
    } else if (b.what == "fx") {
        if (b.on == "player") playBattleFx(b.fx, -1);
        else if (b.on == "monster") playBattleFx(b.fx, t);
        else playBattleFxAt(b.fx, at.x, at.y, true);
    } else if (b.what == "dim") {
        cs.dimMs = settings_.reduceEffects ? 0 : std::max(700, b.ms > 0 ? b.ms : 60000);   // until the move ends (endSkill)
    } else if (b.what == "banner") {
        showBanner(locale_.has(b.text) ? locale_.tr(b.text) : b.text, cs.skill.owner >= 0 ? "line" : "ultimate", b.ms > 0 ? b.ms : 1500);
    } else if (b.what == "name") {
        showBanner(skillName(), "ultimate", b.ms > 0 ? b.ms : 1400);
    } else if (b.what == "namePart") {
        showBanner(toms::namePart(skillName(), cs.skill.steps, d.play.points), "ultimate", b.ms > 0 ? b.ms : 900);
    } else if (b.what == "count") {
        popWord(std::to_string(cs.skill.steps) + "/" + std::to_string(d.play.points), -1, "chain");
    } else if (b.what == "camera") {
        cs.camZoom = settings_.reduceEffects ? 1.0f + (b.zoom - 1.0f) * 0.4f : b.zoom;
        cs.camMs = cs.camMsMax = std::max(300, b.ms);
        cs.camOn = b.on == "monster" ? t : -1;
    } else if (b.what == "clip") {
        if (battleRigReady() && cs.skill.owner < 0) {
            const bool has = battleCharacterAnim_ && !b.clip.empty() && battleCharacterAnim_->find(b.clip);
            setBattleRigState("attack", true, has ? b.clip : std::string());
        }
    } else if (b.what == "hits") {   // the finish's hits share the move's damage (set when the finish began)
        if (cs.skill.phase == P::Finish && cs.skill.owner < 0 && cs.skill.hitDmg > 0) {
            const int count = std::max(1, b.count);
            cs.skill.hitDmg = std::max(1, cs.skill.hitDmg / count);
            cs.skill.hitsLeft = count;
            cs.skill.hitGapMs = std::max(30, b.gapMs);
            cs.skill.hitMs = 0;
        }
    } else if (b.what == "shake") {
        if (!settings_.reduceEffects) {
            cs.shakePx = b.value > 0.0f ? b.value : 8.0f;
            cs.shakeMs = cs.shakeMsMax = b.ms > 0 ? b.ms : 300;
        }
    } else if (b.what == "sound") {
        audio.play(b.fx);
    }
}

void Game::updateSkill(int dtMs) {
    using P = CombatState::SkillRun::Phase;
    CombatState::SkillRun& s = cs.skill;
    const toms::SpecialMove& d = *s.def;
    const int before = s.phaseMs;
    s.phaseMs += dtMs;
    if (s.phase == P::Demo) {
        for (const toms::ShowBeat* b : toms::beatsDue(d.demo, before, s.phaseMs)) skillBeat(*b, skillCentre());
        if (s.phaseMs < toms::showLength(d.demo)) return;
        // the play (16 §6.1 step 4): the move's own kind and numbers
        const toms::PlayKind kind = toms::playKindFromString(d.play.kind);
        toms::TouchPlaySettings ts = touchSettingsFor(kind);
        ts.shape = d.play.shape;
        ts.shapeSize = d.play.size;
        ts.shapeCornerRadius = d.play.cornerRadius;
        ts.shapeTimeMs = ts.mashTimeMs = d.play.timeMs;
        ts.sliceCues = d.play.cues;
        const glm::vec2 c = skillCentre();
        const glm::vec4 area = kind == toms::PlayKind::Shape ? glm::vec4(c.x - 100.0f, c.y - 100.0f, 200.0f, 200.0f) : touchArea();
        cs.touch.start(kind, d.play.points, true, area, (uint32_t)playRng_(), ts);
        touchHeld_ = false;
        s.phase = P::Play;
        s.phaseMs = 0;
        return;
    }
    if (s.phase == P::Play) {
        stepTouchPlay(dtMs);
        if (cs.touch.running()) return;
        // the play is over: what was done counts (Q45: a share of the hits and the damage)
        s.share = (float)cs.touch.result().hits / (float)std::max(1, cs.touch.points());
        s.phase = P::Finish;
        s.phaseMs = 0;
        cs.dimMs = 0;
        if (s.owner < 0) {
            const int t = cs.target;
            const float stagger = t < (int)cs.foes.size() ? cs.foes[(size_t)t].staggerDamage : 1.0f;
            s.hitDmg = (int)std::lround((float)normalHitDamage(t) * d.damage * s.share * stagger);
            if (s.share <= 0.0f) popWord(locale_.tr("battle.move.default.miss"), -1, "move-miss");
        }
        for (const toms::ShowBeat* b : toms::beatsDue(d.finish, -1, 0)) skillBeat(*b, skillCentre());
        return;
    }
    for (const toms::ShowBeat* b : toms::beatsDue(d.finish, before, s.phaseMs)) skillBeat(*b, skillCentre());
    if (s.hitsLeft > 0) {
        s.hitMs -= dtMs;
        while (s.hitMs <= 0 && s.hitsLeft > 0 && cs.skill.on) {
            s.hitsLeft--;
            s.hitMs += s.hitGapMs;
            const int t = cs.target;
            if (t < 0 || t >= (int)cs.foes.size() || cs.foes[(size_t)t].down()) { s.hitsLeft = 0; break; }
            battleEvent("hit", -1, t, s.hitDmg, "skill");
            popDamage(s.hitDmg, t, "perfect");
            feelMoment("hit");
            if (const toms::WeaponFx* fx = battleWeaponFx(true)) playBattleFx(fx->hit, t);
            damageFoe(t, s.hitDmg);
            if (cs.foes[(size_t)t].down()) {   // it fell: the rest of the show is cut short
                const int dmg = s.hitDmg;
                endSkill();   // (resets cs.skill, so s with it)
                afterPlayerHit(std::string(), trParam(locale_.tr("battle.crit_win"), "dmg", std::to_string(dmg)));
                return;
            }
        }
    }
    if (cs.skill.on && s.phaseMs >= toms::showLength(d.finish) && s.hitsLeft <= 0) endSkill();
}

void Game::endSkill() {
    const CombatState::SkillRun s = cs.skill;
    cs.skill = CombatState::SkillRun{};
    cs.dimMs = 0;
    cs.touch.stop();
    cs.touchPaid = false;
    if (!s.def) return;
    if (s.owner < 0) {   // the player's move: counted like an attack (the grade, the chain), then the cooldown
        cs.ultimate = false;
        cs.attacksDone++;
        if (s.share >= 1.0f) { cs.perfects++; cs.chain = std::min(feel_.chainMax, cs.chain + 1); }
        else if (s.share <= 0.0f) cs.chain = 0;
        cs.bestChain = std::max(cs.bestChain, cs.chain);
        const int t = cs.target;
        if (cs.active && t < (int)cs.foes.size() && !cs.foes[(size_t)t].down() && cs.foes[(size_t)t].staggerMs <= 0)
            if (toms::addBreak(cs.foes[(size_t)t].breakGauge, (float)s.steps, foeBreakPoints(t)))
                stunFoe(t, breakSettings_.brokenMs, breakSettings_.brokenDamage, true);
        if (cs.active && cs.pace == toms::Pace::Cooldown) { cs.atkBar.cooling = true; cs.atkBar.cooldownMs = barCooldownMs(); }
        return;
    }
    // a monster's special guard play: its waiting hit lands now, less by the share done (16 §4.3)
    if (s.owner < (int)cs.foes.size() && cs.active) {
        cs.foes[(size_t)s.owner].guardShare = s.share;
        resolveEnemyClockFire(s.owner);
    }
}

// ---- the training ground (16 §5) ----

namespace {
std::string todayString() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}
}  // namespace

int Game::trainingRunsLeft() const {
    const nlohmann::json tr = battleJson_.value("training", nlohmann::json::object());
    const int perDay = std::max(0, tr.value("runsPerDay", 3));
    const int used = meta_.trainDay == todayString() ? meta_.trainRuns : 0;
    return std::max(0, perDay - used);
}

bool Game::startTraining(const std::string& monster) {
    const nlohmann::json tr = battleJson_.value("training", nlohmann::json::object());
    const int cost = std::max(0, tr.value("cards", 1));
    auto it = meta_.cards.find(monster);
    EnemyInst probe;
    if (it == meta_.cards.end() || it->second.copies < cost || trainingRunsLeft() <= 0 ||
        !enemyFromData(monster, nlohmann::json::object(), probe)) {
        audio.play("deny");
        return false;
    }
    it->second.copies -= cost;   // the run spends the card (the owner's rule; Q44)
    const std::string today = todayString();
    if (meta_.trainDay != today) { meta_.trainDay = today; meta_.trainRuns = 0; }
    meta_.trainRuns++;
    markProgressDirty();
    if (inGameMenuOpen_) closePlayerMenu();
    const int hp = pl.hp;
    arena_ = Arena{};
    arena_.on = true;
    arena_.training = true;
    arena_.enemies = {monster};
    arena_.overrides = nlohmann::json::object();   // (the monster as it is; a null here would throw in enemyFromData)
    arena_.fightsMax = std::max(1, tr.value("fights", 3));
    arena_.hpBefore = hp;
    arenaNextFight();
    cs.log = trParam(trParam(locale_.tr("training.start"), "name", probe.name), "n", std::to_string(arena_.fightsMax));
    return true;
}

void Game::endTraining() {
    const Arena a = arena_;
    arena_ = Arena{};
    cs = CombatState{};
    pl.hp = a.hpBefore;
    showNotice(trParam(trParam(trParam(locale_.tr("training.done"), "w", std::to_string(a.wins)), "exp", std::to_string(a.expGained)),
                       "uses", std::to_string(a.usesGained)));
    markProgressDirty();
}

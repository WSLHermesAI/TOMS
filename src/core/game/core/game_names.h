// game_names.h -- every name the game code says: a fighter's or a map object's status and the clip it plays, a battle
// event, a feel moment, a pop-up word's style (the owner, 2026-11-26: "not allow hard code string ... must make a define
// or come from a setting file"; docs/project/12_CPP_NAMING.md §7).
//
// The game code writes toms::state::g_pszIdle, never "idle". A name that only data uses needs no line here: it stays in
// its data file. A new name the code needs is added here first. tools/naming/check_names.py (test lint.game_names)
// fails on a literal equal to any name below in the game's code.
#pragma once

namespace toms {

// A fighter's status and the clip it plays (looks.json states; the clips of a .anim; a weapon's items.json
// "battleAnim" "clips"); a map object's status (map_objects.h).
namespace state {
inline constexpr const char g_pszIdle[]      = "idle";
inline constexpr const char g_pszCharge[]    = "charge";      // the hero's bar filling; a monster charging its special
inline constexpr const char g_pszAttack[]    = "attack";
inline constexpr const char g_pszAttackBig[] = "attack_big";  // the hero's ultimate
inline constexpr const char g_pszMiss[]      = "miss";
inline constexpr const char g_pszHurt[]      = "hurt";
inline constexpr const char g_pszHurtBig[]   = "hurt_big";
inline constexpr const char g_pszDie[]       = "die";
inline constexpr const char g_pszSpecial[]   = "special";     // a monster's special attack pose
inline constexpr const char g_pszPower[]     = "power";       // a monster powering up its special attack
inline constexpr const char g_pszTransform[] = "transform";
inline constexpr const char g_pszParry[]     = "parry";
inline constexpr const char g_pszWin[]       = "win";
inline constexpr const char g_pszWinBig[]    = "win_big";
inline constexpr const char g_pszWinPar[]    = "win_par";
inline constexpr const char g_pszLose[]      = "lose";
inline constexpr const char g_pszOpening[]   = "opening";     // a map object (a door) opening
inline constexpr const char g_pszGone[]      = "gone";        // a map object used up
inline constexpr const char g_pszBroken[]    = "broken";      // a monster whose guard broke
}  // namespace state

// A step of a boss sequence on its strip (game_gauntlet.cpp).
namespace gauntlet_step {
inline constexpr const char g_pszDone[] = "done";
inline constexpr const char g_pszNow[]  = "now";
inline constexpr const char g_pszNext[] = "next";
}  // namespace gauntlet_step

// The battle's event log (toms::BattleEvent::m_strType; --battle-log, the scenarios' "events", a monster's "events").
namespace battle_event {
inline constexpr const char g_pszActive[]     = "active";
inline constexpr const char g_pszBossIntro[]  = "bossIntro";
inline constexpr const char g_pszBreak[]      = "break";
inline constexpr const char g_pszCellIntro[]  = "cellIntro";
inline constexpr const char g_pszCombo[]      = "combo";
inline constexpr const char g_pszDanger[]     = "danger";
inline constexpr const char g_pszDodge[]      = "dodge";
inline constexpr const char g_pszDown[]       = "down";
inline constexpr const char g_pszEdited[]     = "edited";
inline constexpr const char g_pszEvent[]      = "event";
inline constexpr const char g_pszFlag[]       = "flag";
inline constexpr const char g_pszFlee[]       = "flee";
inline constexpr const char g_pszFoeIntro[]   = "foeIntro";
inline constexpr const char g_pszGauntlet[]   = "gauntlet";
inline constexpr const char g_pszGuard[]      = "guard";
inline constexpr const char g_pszGuardBreak[] = "guardBreak";
inline constexpr const char g_pszGuardEnd[]   = "guardEnd";
inline constexpr const char g_pszHeal[]       = "heal";
inline constexpr const char g_pszHeroDown[]   = "heroDown";
inline constexpr const char g_pszHit[]        = "hit";
inline constexpr const char g_pszIntent[]     = "intent";
inline constexpr const char g_pszInterrupt[]  = "interrupt";
inline constexpr const char g_pszLose[]       = "lose";
inline constexpr const char g_pszMove[]       = "move";
inline constexpr const char g_pszPace[]       = "pace";
inline constexpr const char g_pszParry[]      = "parry";
inline constexpr const char g_pszPower[]      = "power";
inline constexpr const char g_pszSay[]        = "say";
inline constexpr const char g_pszShout[]      = "shout";
inline constexpr const char g_pszSkill[]      = "skill";
inline constexpr const char g_pszSpellBreak[] = "spellBreak";
inline constexpr const char g_pszStagger[]    = "stagger";
inline constexpr const char g_pszStart[]      = "start";
inline constexpr const char g_pszStat[]       = "stat";
inline constexpr const char g_pszSummon[]     = "summon";
inline constexpr const char g_pszSuper[]      = "super";
inline constexpr const char g_pszTarget[]     = "target";
inline constexpr const char g_pszTransform[]  = "transform";
inline constexpr const char g_pszTravel[]     = "travel";
inline constexpr const char g_pszUnleash[]    = "unleash";
inline constexpr const char g_pszWin[]        = "win";
}  // namespace battle_event

// A feel moment (battle.json "feel": its hit stop, shake and sound; FeelSettings).
namespace moment {
inline constexpr const char g_pszHit[]       = "hit";
inline constexpr const char g_pszGreat[]     = "great";
inline constexpr const char g_pszPerfect[]   = "perfect";
inline constexpr const char g_pszParry[]     = "parry";
inline constexpr const char g_pszBlock[]     = "block";
inline constexpr const char g_pszBreak[]     = "break";
inline constexpr const char g_pszHurt[]      = "hurt";
inline constexpr const char g_pszHeavyHurt[] = "heavyHurt";
inline constexpr const char g_pszUltimate[]  = "ultimate";
inline constexpr const char g_pszPhase[]     = "phase";
inline constexpr const char g_pszWin[]       = "win";
}  // namespace moment

// A pop-up word's or number's style (its look in the battle UI).
namespace pop_style {
inline constexpr const char g_pszHit[]      = "hit";
inline constexpr const char g_pszGreat[]    = "great";
inline constexpr const char g_pszPerfect[]  = "perfect";
inline constexpr const char g_pszGuard[]    = "guard";
inline constexpr const char g_pszHurt[]     = "hurt";
inline constexpr const char g_pszParry[]    = "parry";
inline constexpr const char g_pszBlock[]    = "block";
inline constexpr const char g_pszEarly[]    = "early";
inline constexpr const char g_pszChain[]    = "chain";
inline constexpr const char g_pszMove[]     = "move-";       // a move's name: "move-" + its tier ("move-hit")
inline constexpr const char g_pszMoveMiss[] = "move-miss";
}  // namespace pop_style

// A monster attack's kind (battle.json "feelFx" "foeAttack").
namespace foe_attack {
inline constexpr const char g_pszStrike[]     = "strike";
inline constexpr const char g_pszSpell[]      = "spell";
inline constexpr const char g_pszSpecial[]    = "special";
inline constexpr const char g_pszBigSpecial[] = "bigSpecial";
inline constexpr const char g_pszBig[]        = "big";
}  // namespace foe_attack

// A hit's tier (a combo's result, battle.json "combo" "tiers"): also its feel moment and its number's style.
namespace tier {
inline constexpr const char g_pszMiss[]    = "miss";
inline constexpr const char g_pszHit[]     = "hit";
inline constexpr const char g_pszGreat[]   = "great";
inline constexpr const char g_pszPerfect[] = "perfect";
}  // namespace tier

// A banner's kind (f_ShowBanner).
namespace banner {
inline constexpr const char g_pszLine[]     = "line";
inline constexpr const char g_pszBreak[]    = "break";
inline constexpr const char g_pszUltimate[] = "ultimate";
}  // namespace banner

// What ends a tutorial step (battle.json "tutorial" "doneOn").
namespace tutorial_done {
inline constexpr const char g_pszHeroHit[] = "heroHit";
inline constexpr const char g_pszDodge[]   = "dodge";
inline constexpr const char g_pszGuarded[] = "guarded";
}  // namespace tutorial_done

// The guard's mode (battle.json "guard" "mode").
namespace guard_mode {
inline constexpr const char g_pszTiming[] = "timing";
inline constexpr const char g_pszWarn[]   = "warn";
inline constexpr const char g_pszBar[]    = "bar";
}  // namespace guard_mode

// A monster move's kind (moves.json "kind"; toms::MoveKind) and the plain move every monster has.
namespace move_kind {
inline constexpr const char g_pszStrike[] = "strike";
inline constexpr const char g_pszHeavy[]  = "heavy";
inline constexpr const char g_pszJabs[]   = "jabs";
inline constexpr const char g_pszSpell[]  = "spell";
inline constexpr const char g_pszGuard[]  = "guard";
}  // namespace move_kind

// The battle's pace (battle.json "pace"; toms::Pace).
namespace pace {
inline constexpr const char g_pszStamina[]  = "stamina";
inline constexpr const char g_pszCooldown[] = "cooldown";
}  // namespace pace

// A special move's use (special_moves.json "use").
namespace move_use {
inline constexpr const char g_pszUltimate[] = "ultimate";
inline constexpr const char g_pszSkill[]    = "skill";
inline constexpr const char g_pszGuard[]    = "guard";
}  // namespace move_use

// A mission's kind and state (missions.json; the save).
namespace mission {
inline constexpr const char g_pszDaily[]     = "daily";
inline constexpr const char g_pszOnce[]      = "once";
inline constexpr const char g_pszLocked[]    = "locked";
inline constexpr const char g_pszAvailable[] = "available";
inline constexpr const char g_pszActive[]    = "active";
inline constexpr const char g_pszCompleted[] = "completed";
inline constexpr const char g_pszClaimed[]   = "claimed";
inline constexpr const char g_pszFlag[]      = "flag";      // an objective: a flag set
}  // namespace mission

// An event's trigger (events "trigger"; 06 section 1).
namespace event_trigger {
inline constexpr const char g_pszStep[]       = "step";
inline constexpr const char g_pszTalk[]       = "talk";
inline constexpr const char g_pszChain[]      = "chain";
inline constexpr const char g_pszVarChanged[] = "varChanged";
}  // namespace event_trigger

// A story action's type (an event's or a dialogue's "actions" [{"type": ...}]; story_actions.h, game_story.cpp).
namespace story_action {
inline constexpr const char g_pszAddStat[]           = "addStat";
inline constexpr const char g_pszSetStat[]           = "setStat";
inline constexpr const char g_pszGive[]              = "give";
inline constexpr const char g_pszTake[]              = "take";
inline constexpr const char g_pszSetVar[]            = "setVar";
inline constexpr const char g_pszAddVar[]            = "addVar";
inline constexpr const char g_pszSetFlag[]           = "setFlag";
inline constexpr const char g_pszSetStoryFlag[]      = "setStoryFlag";
inline constexpr const char g_pszAddCounter[]        = "addCounter";
inline constexpr const char g_pszFireEvent[]         = "fireEvent";
inline constexpr const char g_pszMessage[]           = "message";
inline constexpr const char g_pszStartMission[]      = "startMission";
inline constexpr const char g_pszClaimMission[]      = "claimMission";
inline constexpr const char g_pszMakeChoice[]        = "makeChoice";
inline constexpr const char g_pszEnterBattle[]       = "enterBattle";
inline constexpr const char g_pszUnlockSkill[]       = "unlockSkill";
inline constexpr const char g_pszSetSideStoryState[] = "setSideStoryState";
inline constexpr const char g_pszRescue[]            = "rescue";
inline constexpr const char g_pszStartDialogue[]     = "startDialogue";
inline constexpr const char g_pszOpenStore[]         = "openStore";
inline constexpr const char g_pszHeal[]              = "heal";
}  // namespace story_action

// A chapter beat's kind (story/chapters/*.json "beats" "kind").
namespace story_beat {
inline constexpr const char g_pszEvent[] = "event";
}  // namespace story_beat

// A gesture or a shape drawn in a touch play (special_moves.json "shape", a slice cue; gesture.h).
namespace gesture {
inline constexpr const char g_pszPentagram[] = "pentagram";
inline constexpr const char g_pszTriangle[] = "triangle";
inline constexpr const char g_pszSquare[] = "square";
inline constexpr const char g_pszCircle[] = "circle";
inline constexpr const char g_pszLine[] = "line";
inline constexpr const char g_pszV[] = "v";
inline constexpr const char g_pszM[] = "m";
inline constexpr const char g_pszWay[] = "way";
}  // namespace gesture

// A hub location's action (hub.json "action" "kind").
namespace hub_action {
inline constexpr const char g_pszTalk[] = "talk";
}  // namespace hub_action

// A test scenario's scene (tests/scenarios/*/scenario.json "scene"; docs/gameplay/08).
namespace scene {
inline constexpr const char g_pszTalk[] = "talk";
}  // namespace scene

// A stair's direction (stair links "dir").
namespace stair_dir {
inline constexpr const char g_pszUp[]   = "up";
inline constexpr const char g_pszDown[] = "down";
}  // namespace stair_dir

// A battle-cells rule's start cell (battle.json "cells" "allBut") and a weapon play's way back (weapon_play.json "back").
namespace place {
inline constexpr const char g_pszStart[] = "start";
}  // namespace place

// The start of a text key or an id the code builds from a name ("battle.play_hint_" + a play: "battle.play_hint_bow").
namespace key_prefix {
inline constexpr const char g_pszBattlePlayHint[]  = "battle.play_hint_";
inline constexpr const char g_pszStory[]           = "story.";
inline constexpr const char g_pszMission[]         = "mission.";
inline constexpr const char g_pszStatEffect[]      = "stat.effect.";
inline constexpr const char g_pszEvent[]           = "event_";
inline constexpr const char g_pszPool[]            = "pool_";
}  // namespace key_prefix

}  // namespace toms

# 06 — Floor redesign: a look and a cast per chapter

The 70 floors used to feel the same: every generated floor of a chapter had the same two original monsters, the
same walls and floor, and almost no NPCs. With the AI-made sprites ([../engine/14_AI_SPRITES.md](../engine/14_AI_SPRITES.md)), each
chapter now has its own wall / floor / stairs set, its own six new monsters (one new one on every generated floor),
and five new NPCs. Made by `tools/redesign_floors.py` (2026-10-19).

## 1. The plan

| Ch | Setting → theme | New monsters, in the order they appear (floor 1 … 6) | New NPCs |
|---|---|---|---|
| 1 | Village edge → wood | mossling, sprout imp, toadstool, bear cub, vine snake, pixie | farmer, farmer's wife, shepherd girl, old woman, village boy |
| 2 | Forest path → forest | forest wolf, thorn boar, horn beetle, bandit, kobold, red bat | hunter, lumberjack, ranger, fisherman, village girl |
| 3 | Before the gate → cave | rock crab, mole miner, gem slime, crystal spider, stone lizard, cave troll | miner, dwarf, town guard, gatekeeper, archer scout |
| 4 | The library → crypt | grave rat, glow worm, zombie, mummy, bone archer, ghoul | scholar, cartographer, priest, nun, monk |
| 5 | → shadow | ghost maiden, vampire bat, shadow assassin, banshee, skull knight, lich | oracle, fortune teller, pilgrim, healer, wandering mage |
| 6 | → lava | salamander, magma slime, fire imp, ember wolf, lava golem, flame spirit | blacksmith, armorer, alchemist, potion seller, explorer |
| 7 | → sand | cinder beetle, ash wraith, phoenix chick, fire drake, ore golem, gargoyle | merchant, peddler, jeweler, sailor, bard |
| 8 | → ice | ice slime, frost wolf, penguin knight, frost fairy, yeti, ice golem | innkeeper, baker, weaver, tailor, old hero |
| 9 | → crystal | blizzard owl, ice wraith, crystal serpent, mammoth, dark mage, chimera | elf, miller, fletcher, knight errant, jester |
| 10 | → castle | dark knight, demon guard, minotaur, hellhound, armored ogre, demon general | duke, duchess, young lord, advisor, guard captain |

Each of the 60 new monsters and 50 new NPCs appears once in this table; each of the 10 themes is used once.

## 2. The rules

- **Theme:** the chapter's 7 floors (the boss floor too) get `"theme"` in their stage file. The game draws
  `wall_<t>`, `floor_<t>`, `stairs_up_<t>`, `stairs_down_<t>`.
- **A new monster every floor:** generated floor *k* of a chapter introduces its *k*-th new monster. On that floor,
  the newest monster is the most common (it takes the first changed cell and is twice as likely after that). The
  chapter's earlier new monsters keep appearing. About a third of the monsters stay the original types, at least one
  of each: the missions count slimes, golems and wraiths, and the original monsters have their encounter dialogues.
- **Same layout:** a monster keeps its cell and its save id (`objects."x,y".uid`, [05_SAVE_SYSTEM.md](05_SAVE_SYSTEM.md));
  only its type changes. Keys, doors, gates, events and stairs do not move, so the story check is the same as before
  (0 errors, 0 blocking issues on 70 floors).
- **One new NPC per floor**, except the side-story floor (5 per chapter). Each stands on a reachable floor cell that
  cuts nothing off (checked by path search). It is at least 3 cells from stairs and the start, and alcoves come first.
  The NPC has its draft file with one chat line and a gentle idle animation.
- **Difficulty follows the old curve:** a new monster's stats come from the chapter's original monsters (their average
  hp / atk / def / exp / gold), times 0.96 on the chapter's first floor rising to 1.16 on its sixth. Each chapter's
  baseline is kept at least 10 % above the previous chapter's (late chapters reuse weaker original pairs). New monster
  hp runs from 20 (mossling, ch1) to 106 (demon general, ch10).
- **The floor spec** (`F??.json`) `enemies.mix` lists the monster types the floor now has.

## 3. Floor tiles

Six of the AI floor tiles (cave, crypt, shadow, lava, crystal, castle) had a strong shape in the middle. Repeated over
a map, it made a busy grid that fought with the characters. They were remade in code from their own colours: a
chunky 2-pixel speckle on the tile's main colour, with a faint seam for stone floors, and the lava floor lifted so the
walls stand out. The busy versions are kept in `AIGamestyle/output/toms_sprites/_first/`. Wood, forest, sand and ice
were kept as made.

## 4. Running it again

The script skips a floor that already has a theme, so it runs once. To redesign again:
1. restore the floors (`git checkout -- assets/data/story/floors assets/data/stages`);
2. edit the tables at the top of `tools/redesign_floors.py`;
3. run `python tools/redesign_floors.py --dry-run` to see each floor's change, then without `--dry-run`.

Placed objects can be changed by hand in the stage editor afterwards as usual.

## 5. Not done / limits

- **Boss floors** keep their hand-made maps and monsters (only the theme changed).
- **The `dark16` art style** has no dark versions of the new sprites; they show in their normal colours there.
- **The new monsters and NPCs are drafts:** tier-based names, one chat line, no encounter dialogue.
- **Screenshots:** every game screenshot changes (new walls, floors, monsters). The reference images were removed in
  `795385a`; new ones must be made (`TOMS_UPDATE_GOLDEN=1`) once the look is approved.
- Floor names for chapters 5–10 are still "Floor 29" and so on.

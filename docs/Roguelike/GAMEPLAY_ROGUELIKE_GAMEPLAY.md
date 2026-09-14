# TOMS Roguelike Gameplay Design

## 1. Product Goal

Turn TOMS into a roguelike game where each run is driven by:
- procedurally generated maze floors
- stage selection before the run begins
- in-run shop decisions
- skill point build choices
- 1 to 3 bosses depending on difficulty
- persistent meta progression between runs

## 2. Core Loop

1. Select a stage
2. Pick a difficulty
3. Enter floor 1 of the stage
4. Clear enemies, explore maze branches, collect rewards
5. Visit shop or event rooms
6. Spend gold and skill points
7. Reach the boss floor
8. Beat the boss or die
9. Return to hub and save meta progress

## 3. Stage Structure

Each stage is a self-contained roguelike route.

### Example stage layout
- Stage has N floors
- Each floor uses its own maze seed
- Easy: 1 boss
- Normal: 2 bosses
- Hard: 3 bosses

### Stage types
- Ruins: beginner-friendly, more chests, easier enemies
- Lava Depths: narrow maps, high damage, stronger bosses
- Ancient Academy: more events and skill books
- Dark Fortress: boss-heavy and combat-heavy

## 4. Player Choices That Matter

### 4.1 Skill point choices
Skill points should not only increase stats. They should unlock build paths.

Good skill point effects:
- unlock new active skills
- upgrade skill rank
- add passive bonuses
- improve exploration capabilities
- improve shop or economy effects

### 4.2 Shop choices
The shop should be a strategic node, not just a healing stop.

Shop categories:
- consumables
- weapons
- armor
- skill books
- key items
- maze tools like reveal map, bomb wall, teleport, key, trap detector

### 4.3 Risk/reward choices
Every floor should offer decisions:
- continue deeper for better rewards
- detour into side rooms for loot
- spend gold now or save for a later shop
- buy a skill book or keep money for healing

## 5. Boss Design

Bosses should be tied to stage difficulty.

- Easy: 1 boss as a checkpoint or tutorial boss
- Normal: 2 bosses, one mechanical and one pressure-based
- Hard: 3 bosses, with increasing pattern complexity

Bosses can also modify maze rules:
- close doors
- spawn chase enemies
- reveal or hide paths
- create escape phases

## 6. Experience Targets

The game should feel like:
- a fixed visual style with a changing tactical layout
- a readable maze with meaningful branching
- a build-focused roguelike rather than pure action
- a run that is short enough to replay but deep enough to master

## 7. Gameplay Pillars

1. **Replayability**: every run is different
2. **Build choice**: skill points and shop items shape the character
3. **Maze exploration**: branching paths matter
4. **Boss milestones**: stage difficulty is readable and progressive
5. **Persistence**: meta progress makes repeated runs rewarding

## 8. Main Menu Flow

- Continue run
- Start new run
- Select stage
- Select difficulty
- View meta skill tree
- View inventory and unlocked content

## Sources

[1] https://en.wikipedia.org/wiki/Roguelike
[2] https://en.wikipedia.org/wiki/Maze_generation_algorithm
[3] https://en.wikipedia.org/wiki/Loop-erased_random_walk

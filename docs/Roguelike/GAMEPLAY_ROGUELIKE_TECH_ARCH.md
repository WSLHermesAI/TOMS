# TOMS Roguelike Technical Architecture

## 1. System Overview

The game should be split into systems so that each part is data-driven:
- StageSystem
- MazeGenerator
- RoomPlacementSystem
- EncounterSystem
- ShopSystem
- SkillTreeSystem
- BattleSystem
- RewardSystem
- SaveSystem
- MetaProgressSystem

## 2. Maze Generation Flow

### 2.1 Generation steps
1. Load stage config
2. Seed RNG
3. Build logical grid
4. Generate spanning tree with Wilson's algorithm
5. Convert the tree into pathable rooms and corridors
6. Place special rooms
7. Spawn enemies, chests, events, and shop content
8. Serialize the result as a MazeInstance

### 2.2 Real-time generation
Do not generate the whole game world at startup.

Recommended approach:
- generate one floor when the player enters it
- store only the seed and floor configuration for replay
- keep a full snapshot only when the player saves mid-run

### 2.3 Import/export strategy
Support two forms:
- seed-only reconstruction
- full snapshot restoration

## 3. Runtime State Model

Runtime state is the current run only.
It should include:
- current stage id
- current floor index
- maze seed
- current HP / MP / gold
- inventory
- equipped skills
- current room
- defeated enemies
- shop state
- boss state

## 4. Persistence Model

### 4.1 Meta save
Meta save stores long-term progress:
- unlocked stages
- unlocked skills
- permanent stat bonuses
- best clears
- global currency

### 4.2 Run save
Run save stores the current run:
- maze instance seed and placement
- floor-by-floor progress
- battle state
- inventory and shop state
- current HP and status effects

### 4.3 Save format recommendations
- JSON for development and debugging
- binary or compressed format for release builds
- version fields for migrations
- atomic write through temp file + rename

## 5. Battle System Architecture

### 5.1 Battle state machine
Battle should be run by a state machine:
- init
- player choose action
- resolve action
- enemy AI turn
- apply status effects
- check victory or defeat
- reward and exit

### 5.2 Battle data model
Each unit should have:
- unitId
- team
- HP
- attack
- defense
- speed
- skill list
- status effects
- AI id for enemies

### 5.3 Battle scene types
- normal battle
- elite battle
- boss battle
- event battle
- escape battle

## 6. Skill System Architecture

### 6.1 Skill point logic
Skill points should drive build choices, not just raw power.

Possible uses:
- unlock skill tree nodes
- upgrade active skills
- improve passive bonuses
- unlock utility perks

### 6.2 Skill tree shape
Use a node graph structure:
- each skill has id, cost, prerequisites, effects, tags
- nodes can branch by combat, survival, exploration, and economy

## 7. Shop System Architecture

### 7.1 Shop rules
The shop should use:
- item slots
- stock counts
- per-run buy limits
- rarity weighting
- stage or floor-based restock

### 7.2 Shop balance rules
- consumables can repeat with limits
- skill books are usually one-time buys
- rare upgrade items should be limited
- shop inventory should scale by difficulty

## 8. Difficulty Scaling

Difficulty affects:
- enemy HP multiplier
- enemy damage multiplier
- boss count
- shop frequency
- price multiplier
- reward quality
- skill point gain rate

## 9. Recommended Engine Boundaries

Keep gameplay systems independent from rendering.

Suggested separation:
- engine handles rendering/input/audio
- gameplay layer handles run state, maze, combat, and persistence
- data files define stage and item content

## 10. Future-Proofing

Add these early:
- schemaVersion
- saveVersion
- algorithmVersion for maze generation
- contentVersion for balancing updates

## Sources

[1] https://en.wikipedia.org/wiki/Roguelike
[2] https://en.wikipedia.org/wiki/Maze_generation_algorithm
[3] https://en.wikipedia.org/wiki/Loop-erased_random_walk

# TOMS Roguelike Data Schema Appendix

## 1. Stage Definition

```json
{
  "stageId": "stage_01",
  "name": "Ruins of Dawn",
  "theme": "ruins",
  "difficultyPool": ["easy", "normal", "hard"],
  "floorCount": 5,
  "bossCountByDifficulty": {
    "easy": 1,
    "normal": 2,
    "hard": 3
  },
  "maze": {
    "width": 31,
    "height": 31,
    "algorithm": "wilson",
    "roomDensity": 0.35,
    "specialRoomCount": 8
  }
}
```

## 2. Maze Instance

```json
{
  "mazeId": "stage_01_floor_03_seed_845112",
  "stageId": "stage_01",
  "floorIndex": 3,
  "seed": 845112,
  "algorithm": "wilson",
  "width": 31,
  "height": 31,
  "cells": [],
  "rooms": [],
  "edges": [],
  "exploredMask": []
}
```

### Maze rule
- `cells`: low-level wall/passable map
- `edges`: graph connectivity
- `rooms`: gameplay nodes such as start, shop, event, boss
- `exploredMask`: player reveal state

## 3. Room Definition

```json
{
  "roomId": "shop_01",
  "roomType": "shop",
  "gridRect": { "x": 9, "y": 7, "w": 3, "h": 3 },
  "neighbors": ["r04"],
  "contentId": "basic_shop_a"
}
```

Room types:
- start
- normal
- elite
- shop
- event
- chest
- boss
- exit

## 4. Item Definition

```json
{
  "itemId": "potion_small",
  "name": "Small Potion",
  "itemType": "consumable",
  "rarity": "common",
  "price": 30,
  "maxStack": 9,
  "buyLimitPerRun": 5,
  "effects": [
    { "type": "heal", "value": 25 }
  ]
}
```

## 5. Skill Definition

```json
{
  "skillId": "skill_fireball_02",
  "name": "Fireball II",
  "tree": "combat",
  "tier": 2,
  "cost": 2,
  "prerequisites": ["skill_fireball_01"],
  "effects": [
    { "type": "damageAdd", "value": 12 },
    { "type": "aoeRadiusAdd", "value": 1 }
  ]
}
```

## 6. Unit Definition

```json
{
  "unitId": "player_01",
  "team": "player",
  "hp": 100,
  "maxHp": 100,
  "attack": 18,
  "defense": 6,
  "speed": 10,
  "critRate": 0.12,
  "critDamage": 1.5,
  "skills": ["skill_slash_01"],
  "statusEffects": []
}
```

## 7. Shop Instance

```json
{
  "shopId": "basic_shop_a",
  "shopType": "stage_shop",
  "itemSlots": 6,
  "restockRule": "floor_entry",
  "items": [
    { "slot": 0, "itemId": "potion_small", "price": 30, "stock": 3 },
    { "slot": 1, "itemId": "bomb_basic", "price": 45, "stock": 2 }
  ]
}
```

## 8. Run Save

```json
{
  "runId": "run_20260905_001",
  "playerId": "player_main",
  "currentStageId": "stage_01",
  "currentFloorIndex": 3,
  "currentMazeId": "stage_01_floor_03_seed_845112",
  "hp": 86,
  "maxHp": 120,
  "gold": 240,
  "skillPoints": 2,
  "inventory": [
    { "itemId": "potion_small", "count": 3 }
  ],
  "equippedSkills": ["skill_dash_01"]
}
```

## 9. Meta Save

```json
{
  "playerId": "player_main",
  "version": 1,
  "globalLevel": 14,
  "exp": 18320,
  "unlockPoints": 5,
  "unlockedStages": ["stage_01", "stage_02"],
  "unlockedSkills": ["skill_dash_01"],
  "metaUpgrades": {
    "maxHpBonus": 20,
    "goldBonus": 0.08,
    "shopDiscount": 0.05
  }
}
```

## 10. Recommended Version Fields

Always include:
- `schemaVersion`
- `saveVersion`
- `algorithmVersion`
- `contentVersion`

These fields make it possible to migrate old saves after design changes.

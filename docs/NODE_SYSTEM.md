# Node Scene-Graph System

A 2D scene-graph ported from the FM79979 engine's `Frame` (`Src/FM79979Engine/
Core/GameplayUT/Render/CommonRender/Frame.{h,cpp}`), adapted to 2D + **glm**.
Inspired by: https://github.com/fatmingwang/FM79979/blob/master/Src/FM79979Engine/Core/GameplayUT/Render/CommonRender/Frame.h

## Concept
Every object is a **Node** with:
- a **parent**, a **first child**, and a **next sibling** (intrusive tree),
- a **local transform** (`glm::mat3` affine: columns = [right, up, translation]),
- a **cached world transform** that is recomputed lazily only when dirty.

```
world = (parent ? parent->GetWorldTransform() * local : local)
```

This mirrors `Frame`: setting a local transform marks the node **and all
descendants** dirty; `GetWorldTransform()` refreshes the cache on demand via the
sentinel `NODE_DIRTY_WORLD_CACHE = 1e10` on `world[0][0]`.

## API (`src/node.h`, namespace `toms`)
- **Hierarchy**: `AddChild`, `AddChildToLast`, `SetParent(nullptr)` to unparent,
  `SetNextSibling`, `GetParent` / `GetFirstChild` / `GetNextSibling`,
  `GetChildCount`, `IsChild`, `IsAncestor`.
- **Local transform**: `SetLocalTransform`, `SetLocalPosition`, `SetLocalScale`,
  `SetLocalRotation` (Z), `GetLocalTransform` / `GetLocalPosition`.
- **World transform**: `GetWorldTransform`, `SetWorldTransform` (decomposes via
  parent inverse), `GetWorldPosition` / `SetWorldPosition`, `WorldPoint(local)`,
  `WorldRect(size)` (axis-aligned pixel rect of a local box — what the renderer
  consumes as `Quad.rect`).
- **Traversal**: `Visit(fn)` (depth-first: child-deep then siblings),
  `ForEachNode(root, fn)`.
- **Visibility**: `SetVisible` / `IsVisible`.

## Usage in TOMS
`Game::drawInventory()` builds a `uiRoot → panel → slot[0..N]` node tree each
frame; the panel is positioned at screen center and each slot is a child node,
so every slot's on-screen rectangle comes from `slotNode.WorldRect(cell)` — moving
or scaling the panel cascades to all slots automatically.

## Verification
`src/node_test.cpp` (headless, no rendering) builds a parent→child→child tree and
asserts: world = parent×local cascade, root-move cascades to children, dirty-cache
recompute, scale/rotation, 4-node traversal, reparent, and `SetWorldTransform`
decomposition. Build + run:
```bash
cmake -S . -B build-linux && cmake --build build-linux --target node_test
./build-linux/node_test      # -> "node_test: ALL PASS (8 checks)"
```

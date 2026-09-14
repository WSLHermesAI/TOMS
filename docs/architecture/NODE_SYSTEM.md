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

---

# Render binding (`src/scene.h`)

Rendering is **bound to the scene-graph** so the visibility rule "parent invisible =
skip whole subtree" and "draw order = tree order" fall out for free.

- **`GameObject : Node`** — adds `virtual Render(IRenderer*)` and `RenderTree()`:
  ```cpp
  void RenderTree(IRenderer* ren) {
      if (!m_visible) return;            // subtree culled in one flag
      Render(ren);
      for (Node* c = GetFirstChild(); c; c = c->GetNextSibling())
          static_cast<GameObject*>(c)->RenderTree(ren);
  }
  ```
- **`SpriteNode : GameObject`** — the **common render**: you only assign a texture
  (`uv`), a `size` and a `tint`; it draws itself at its node's world rect. No custom
  code needed for map sprites, item icons, slots, etc.
- **`TextNode : GameObject`** — draws a string at the node's world position; the
  actual glyph rasterization is delegated to `TextNode::Draw` (a function pointer the
  game sets once, since the font lives in `Game`).
- **`FullScreenSplash : GameObject`** — a full-screen colored quad (the focus splash).

### How the item UI uses it
`Game::drawInventory()` builds a node tree and renders it with ONE call:
```
modalRoot (visible = invOpen)          // the LAST node drawn in draw() -> on top
 ├─ FullScreenSplash                    //   hidden automatically when modalRoot.visible=false
 └─ panel (local pos = screen center)
     ├─ SpriteNode panelBg
     ├─ TextNode title
     ├─ slot[i] : SpriteNode
     │    ├─ SpriteNode icon   (child of slot -> inherits slot's world transform)
     │    └─ SpriteNode highlight
     └─ TextNode description[0..2]
modalRoot.RenderTree(ren);
```
Opening/closing the inventory is a single `modalRoot.SetVisible(true/false)` —
because the splash is a **child** of `modalRoot`, it is culled together with the rest.
Moving or scaling `panel` cascades to every slot/icon/label since they are its
children. This is exactly the "common render assigns a texture and draws; special
objects override Render()" pattern you described.
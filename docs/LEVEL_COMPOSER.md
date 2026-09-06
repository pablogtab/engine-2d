# Level Composer

`Level Composer` is an editor-only workflow for assembling 2D scenes from a
project's textures and prefabs. It does not add a new runtime representation:
atlas stamps become ordinary entities with `Transform` and `Sprite Renderer`,
and prefab stamps remain normal prefab instances.

## Recommended workflow

1. Open the **Level** workspace. Keep the floor on one or more Tilemap entities
   and use **Level Composer** for walls, arches, columns, debris, vegetation and
   other pieces whose source rectangles are not square tiles.
2. Drag a texture from **Asset Browser** onto **Level Composer**. Configure the
   atlas cell size, margin and spacing, then click or drag across cells to select
   a rectangular source region. Disable `Snap slice to grid` for irregular
   regions and adjust `Region X/Y/W/H` for exact cropping.
3. For an atlas stamp, set its world size, pivot, snap, spacing, sorting layer
   and `Depth Z`. For a prefab stamp, set only the placement values; use
   **Open Prefab** to edit its internal sprites and colliders. Double click the
   stamp or press **Stamp in viewport** to activate it.
4. Click once to place one piece. Drag to place an aligned run; the full run is
   one Undo entry. Use `Q`/`E` for 90-degree rotation, `X`/`Y` for sprite flips
   and `Esc` to cancel the current run and leave stamp mode.
5. Use **Use selected as parent** before stamping to keep a sector or layer
   container as the stable parent. New stamps do not change that target.
6. Save the palette under `Assets/LevelPalettes/*.levelpalette`. References are
   stored by asset handle so texture and prefab moves remain valid.

The Tilemap palette follows the selected Tilemap's actual atlas and pixel cell
size. Dropping a texture or changing the cell size edits that component through
Undo/Redo and marks the scene dirty.

## Walls and character occlusion

A tall 3/4-view wall should not be authored as one indivisible sprite. Split it
into two visual pieces:

- **Back wall** contains the surface that always stays behind actors.
- **Front wall** contains the nearby lip, arch, column face or masonry that must
  cover an actor walking behind it.

Use the layer presets consistently for every tall wall assembly:

| Piece | Sorting layer | Purpose |
| --- | ---: | --- |
| Back wall/base | `0` | Remains behind the character and establishes the wall body. |
| Actor/Prop | `1` | Occupies the walkable space between the two wall pieces. |
| Front wall | `3` | Covers the character when it passes behind the wall. |

Set wall pieces to the `Ground` pivot, `(0.5, 1.0)`, so their bottom-center
anchor lies on the ground-contact line. The actor should use the same
bottom-center convention and set its Sprite Renderer `Sorting layer` to `1`.
Engine2D resolves world-sprite order with this priority:

1. `Sorting layer` establishes the coarse group.
2. World `Z` separates depth planes inside that group.
3. World `Y` orders sprites that share both layer and Z.
4. `Order in layer` resolves the remaining tie.

For Y-sort to work between a wall and an actor, their `Depth Z` must match.
Keep the normal world plane at `0` unless the whole group intentionally uses a
different Z. `Render layer` is independent from visual depth: it controls
camera masks and per-layer effects.

Place the collider only over the wall's footprint at ground level: the strip or
base that actually blocks movement. Do not extend it over the full visual
height. Keep a single collider on the structural entity or reusable prefab so
the front piece does not duplicate the collision response.

When drawing a repeated wall section, drag in the viewport to preview the full
aligned run. Entities are committed when the mouse is released, as one editor
history transaction; one **Undo** removes the complete run. Press `Esc` before
release to discard the preview.

One large sprite cannot represent both sides of an occlusion boundary. When a
character can walk around a corner, split the corner into enough back/front
pieces that each piece has one unambiguous ground-contact line. Physics
colliders remain separate authored components or prefab content.

## Palette asset contract

`.levelpalette` is versioned JSON containing categories and two entry kinds:

- `sprite_region`: texture handle, pixel rectangle, world size and pivot.
- `prefab`: prefab handle.

Both kinds store snap, stroke spacing, rotation and `Depth Z`. Sprite-region
entries also store sorting/order/render-layer values and horizontal and
vertical flip. A prefab entry does not override the render settings of its
children; open the prefab and configure each internal sprite's sorting layer,
pivot and render layer there.

Author reusable prefabs with a neutral container as the root: no visible
sprite, position `(0, 0, 0)`, rotation `0` and scale `(1, 1)`. Put the back wall,
front occluder and any decorative sprites below that root, with their own
pivots and sorting values. Keep Y-sorted sprite children at local Z `0`, so the
root's stamped `Depth Z` remains their shared plane. Keep the blocking collider
on the appropriate child at the ground footprint. The composer then
moves/rotates the neutral root and applies `Depth Z` without flattening the
prefab's internal visual order.

Use **Open Prefab** from a selected prefab stamp to enter Prefab Stage. It uses
the editor's normal unsaved-scene protection before changing context. Save the
prefab, return to the scene, and continue stamping instances from the same
palette entry. Paths are diagnostic fallbacks; stable handles are authoritative
after the asset has been saved.

## Current limits

- Wall topology and transitions are selected manually; there is no neighbor
  autotiling yet.
- The atlas slicer validates rectangle bounds but does not infer the intended
  object from alpha pixels.
- Front-wall fading when a player becomes fully hidden is a separate runtime
  presentation feature and is not generated by the composer.
- Collider generation is intentionally not inferred from artwork. Use prefabs
  for reusable wall pieces with explicit colliders.

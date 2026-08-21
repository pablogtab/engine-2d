# Renderer and Canvas Contracts

This document is the architectural entry point for work that crosses the renderer,
display, Canvas layout, UI input, or Scene View authoring. Update it when one of
these contracts changes; do not infer a new ownership model from `GenericScene`.

## Frame composition

The frame order is fixed:

```text
SceneGeometry -> LocalEffects -> Lighting -> GlobalPostProcess -> CanvasUI -> Presentation
```

- World sprites enter `SceneGeometry`. Single-pass materials remain on this direct path.
- Entity and layer multipass recipes use pooled surfaces in `LocalEffects`.
- Lighting composites onto the captured scene before global post-processing.
- Camera-wide effects run in `GlobalPostProcess`.
- Screen-space UI is submitted in `CanvasUI`, after camera effects, so it is not
  unintentionally altered by them.
- `Presentation` maps the composed source to the physical output.
- `RenderStageMetrics` must continue to describe each stage independently.

`Renderer2D` owns stage ordering, batching, and transient render targets. Components
and systems submit intent; they must not create a parallel scene-specific pipeline.

## Render view and display data

`RenderViewConfiguration` is the boundary delivered to a scene when a render target
changes. It contains:

- `output_width` / `output_height`: pixels in the current render target.
- `reference_width` / `reference_height`: the authored presentation reference.
- `safe_area`: insets expressed in the output coordinate system delivered to that view.
- `show_safe_area_overlay`: editor-only visualization state.

The runtime data path is:

```text
SDL safe-area/window event
  -> Window event classification
  -> DisplayManager refresh
  -> Application view mapping
  -> SceneManager::configure_view
  -> Scene::on_viewport_resize
  -> Canvas layout, rendering, and UI input
```

SDL reports window-logical bounds. `SDLDisplayBackend` converts them to physical
pixel insets before storing `DisplayState`. Fixed-resolution views map those insets
into their reference target; responsive views keep physical output coordinates.
Safe-area data is runtime state and is never serialized into a scene or prefab.

## Canvas viewport rectangles

`calculate_canvas_viewport` is the shared pure contract used by rendering and input.
Its `CanvasViewport` fields have distinct meanings:

- `output_rect`: the complete target or Scene View Canvas frame.
- `platform_safe_rect`: the safe region reported by the platform, regardless of
  whether the Canvas respects it. The editor overlay draws this rectangle.
- `safe_rect`: the effective layout region. It equals `platform_safe_rect` when
  `CanvasComponent::use_safe_area` is enabled and `output_rect` otherwise.
- `physical_rect`: the output-space rectangle to which Canvas coordinates map after
  applying Fit, Fill, Stretch, or Responsive scaling.
- `logical_rect`: the authored Canvas coordinate system.
- `scale`: logical-to-output scale used by both rendering and pointer conversion.

Rendering, hit testing, selection, and gizmos must consume the same resolved geometry.
Never reproduce anchor or safe-area math in an individual caller.

## Rect Transform and rotation

UI layout is split deliberately between two existing components:

- `UILayoutComponent` owns anchors, pivot, anchored position, and size delta.
- `TransformComponent::rotation` owns local Z rotation. The editor exposes this field
  inside `Rect Transform`; no duplicate UI rotation field is serialized.

The Canvas root `TransformComponent` positions the stable authoring preview in Scene
View. Runtime Canvas content remains screen-space. Descendant UI rotations compose
through the authored hierarchy around each element pivot.

`resolve_canvas_hierarchy` is authoritative for the resolved pivot, parent rotation,
total rotation, and screen rectangle. The same result drives:

- UI image quads and text glyph groups;
- pointer hit testing and button interaction;
- Scene View picking;
- the Rect Transform outline, handles, and rotation gesture.

`Renderer2D::submit_screen_sprite` receives an anchor position plus a normalized
pivot; rotation occurs around that pivot. `submit_screen_text` rotates the glyph
group around the resolved UI pivot while retaining per-glyph atlas geometry.

## Editor mutation rules

Canvas frames and safe-area overlays are view-only state. Authored UI edits are not:

- Inspector edits use `EditorPropertyTransaction`.
- A continuous gizmo gesture creates one command on release.
- Escape restores the exact pre-gesture component state.
- Undo and Redo must reproduce layout and rotation without depending on current
  viewport zoom or platform safe-area values.

## Ownership boundaries

- `CanvasLayout` owns pure coordinate resolution.
- `CanvasUISystem` owns Canvas drawing and font caching.
- `UIInputSystem` owns transient hover, capture, focus, and click state.
- `DisplayManager` owns current platform display state and change notifications.
- `Application` maps display state into render-view configuration.
- `GenericScene` only connects these reusable systems while it remains the temporary
  integration scene. It is not the owner of any renderer or Canvas contract.

## Regression coverage

Changes to this area should update the smallest relevant focused suite:

- `CanvasLayoutTests`: viewport mapping, safe areas, nested transforms, rotation.
- `UIInputTests`: resolved-geometry interaction and rotated hit testing.
- `CanvasUIRenderTests`: final screen-space pixels and ordering.
- `DisplayManagerTests`: display-state propagation and notifications.
- `RenderPipelineTests`, `PostProcessTests`, or `GPURenderSmokeTests`: stage order,
  target lifetime, and backend integration.

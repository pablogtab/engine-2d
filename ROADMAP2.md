# Roadmap 2 — Editor and Authoring Workflow

This roadmap extends the current engine roadmap with the systems needed to make the integrated IDE a reliable content-authoring tool. Work is ordered by dependency and user-facing impact.

## Guiding Rules

- Every content mutation made from the editor must support undo and redo.
- Scene data must remain serializable, deterministic, and independent from runtime pointers.
- Authoring features belong in the editor; reusable runtime capabilities belong in the engine core.
- Complete and validate one module before starting the next one.

---

## Module A — Sprite Authoring Essentials

**Goal:** Make common visual adjustments obvious, fast, and production-ready.

- [ ] Add an explicit `Opacity` slider (0–100%) bound to `SpriteComponent::color.a`, while keeping RGB color editing separate.
- [ ] Expose `TransformComponent::pivot` in the Inspector with presets: Center, Top, Bottom, Left, Right, and custom values.
- [ ] Add `Sorting Layer` and `Order in Layer` to sprite rendering and scene serialization.
- [ ] Expose Z-depth with a clear label and explain its relationship to parallax and sorting.
- [ ] Make sprite flip, visibility, tint, outline, and material properties use the common compact property grid.
- [ ] Register every visual property edit as a single reversible command.

**Completion criteria:** A designer can control color, opacity, pivot, depth, and draw order from the Inspector and undo every change.

---

## Module B — Transform and Viewport Tools

**Goal:** Provide direct, predictable scene manipulation.

- [x] Add rotate and scale gizmos in addition to translation.
- [x] Add a World / Local gizmo-space toggle.
- [x] Add grid snapping, angle snapping, and configurable snap increments.
- [ ] Add viewport rulers, guides, and optional safe-area overlays.
- [x] Add alignment and distribution commands for multi-selection.
- [x] Add viewport shortcuts for frame selection, zoom to fit, and reset view.
- [x] Decouple the free-form Scene View from configurable Game output resolution and presentation.
- [ ] Add entity lock and editor-only visibility states.
- [x] Make rotated bounds, picking, and marquee selection use the same transformed geometry.

**Completion criteria:** Objects can be positioned, aligned, rotated, scaled, locked, and selected precisely without leaving the viewport.

---

## Module C — Hierarchy and Inspector Workflow

**Goal:** Make large scenes manageable and consistent.

- [ ] Implement true multi-selection editing for common component properties.
- [ ] Add group/container entities and group-aware duplication.
- [ ] Add hierarchy commands for duplicate, reparent, reorder, lock, and visibility.
- [ ] Add component add/remove commands with complete undo/redo support.
- [ ] Migrate Sprite, RigidBody, Collider, Tilemap, and Animator Inspector controls to the common command-backed edit lifecycle.
- [x] Make Camera 2D an editable ECS entity that drives the runtime view, supports stable follow targets, and participates in undo/redo.
- [ ] Add component search, favorites, and contextual documentation/tooltips.
- [ ] Show scene dirty state and provide Save All.

**Completion criteria:** All content changes from Hierarchy and Inspector are reversible, discoverable, and safe across complex entity trees.

---

## Module D — Asset Pipeline and Prefabs

**Goal:** Turn raw files into reusable, configurable game content.

- [ ] Add drag-and-drop from Asset Browser to Viewport, Hierarchy, and Inspector fields.
- [ ] Add asset import settings: filtering, pixels per unit, pivot, compression, and metadata.
- [ ] Introduce stable asset references (path or asset ID) in serialized scene data.
- [ ] Add asset validation and clear missing-asset diagnostics.
- [ ] Add prefab assets, reusable entity templates, and per-instance overrides.
- [ ] Add prefab apply/revert workflow and dependency-safe reload behavior.

**Completion criteria:** Assets can be placed, configured, reused, and serialized without runtime-pointer fragility.

---

## Module E — Scene Reliability and Project Workflow

**Goal:** Protect authoring work and make scenes safe to evolve.

- [ ] Add autosave with configurable interval and recovery on restart.
- [ ] Warn about unsaved scenes before loading, switching, or closing.
- [ ] Add Save As, Save All, recent scenes, and scene metadata.
- [ ] Add a Scene Validator for missing textures, invalid colliders, absent primary cameras, broken hierarchy references, and invalid tilemaps.
- [ ] Add a diagnostics panel with actionable fix buttons when possible.
- [ ] Add editor settings persistence for workspace, snapping, autosave, and view preferences.

**Completion criteria:** Scene editing survives interruption and reports invalid project state before export.

---

## Module F — Runtime Authoring Systems

**Goal:** Expose core game systems through the editor.

- [ ] Implement dynamic 2D lighting: ambient light, point lights, colors, radius, intensity, and blend mode.
- [ ] Add editor light gizmos and a dedicated Light Inspector section.
- [ ] Implement 2D shadow casters from colliders and static geometry.
- [ ] Add render/sorting layers shared by sprites, lights, particles, and tilemaps.
- [ ] Add a Canvas/UI system for HUDs: text, images, buttons, anchors, and responsive layout.
- [ ] Add UI authoring panels and scene serialization for Canvas content.

**Completion criteria:** Lighting, render order, and game UI can be authored visually and saved in scenes.

---

## Module G — Scripting and Play Mode

**Goal:** Enable rapid gameplay iteration without recompiling C++.

- [ ] Add Lua runtime integration and `ScriptComponent`.
- [ ] Bind entities, transforms, input, audio, physics, events, and scene loading to scripts.
- [ ] Add script creation, attachment, editing, error reporting, and hot reload in the IDE.
- [ ] Add Play in Editor apply-back controls for explicitly selected changes.
- [ ] Preserve the current safe default: Stop restores the original authoring snapshot.
- [ ] Add runtime inspection for script fields and public variables.

**Completion criteria:** Gameplay behavior can be authored, debugged, and reloaded from the editor without compromising authoring state.

---

## Module H — Debugging, Profiling, and Export

**Goal:** Make projects diagnosable and shippable.

- [ ] Add physics debug overlays: colliders, contacts, velocities, centers of mass, and collision layers.
- [ ] Add entity-scoped logs and script error locations.
- [ ] Add frame profiling for update, render, physics, UI, memory, draw calls, and culling.
- [ ] Add debugging controls such as pause, step frame, timescale, and runtime entity inspection.
- [ ] Implement standalone build/export for macOS, Windows, and Linux.
- [ ] Package required assets, validate export configuration, and provide build logs.

**Completion criteria:** Developers can diagnose performance and correctness problems, then export a self-contained game build.

---

## Suggested Execution Order

1. Module A — Sprite Authoring Essentials
2. Module B — Transform and Viewport Tools
3. Module C — Hierarchy and Inspector Workflow
4. Module E — Scene Reliability and Project Workflow
5. Module D — Asset Pipeline and Prefabs
6. Module F — Runtime Authoring Systems
7. Module G — Scripting and Play Mode
8. Module H — Debugging, Profiling, and Export

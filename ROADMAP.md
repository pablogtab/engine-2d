# Roadmap 2 — Engine and Editor Production Path

This is the active roadmap for evolving the current engine prototype into a reliable, scalable authoring environment. It reflects the systems already implemented, the remaining product work, and the architectural gates that must be crossed before adding features that would otherwise multiply technical debt.

The engine is intentionally incomplete. An unchecked item is not automatically a defect: it is planned work. Reengineering is required only when the current design would make the next roadmap modules fragile, repetitive, or difficult to validate.

## Status Rules

- `[x]` means implemented and validated at the level required by the current phase.
- `[ ]` means pending, incomplete, or implemented visually but not yet meeting its completion criteria.
- A module is complete only when its behavior, undo/redo rules, serialization, and relevant tests are complete.
- `GenericScene` remains a temporary integration helper until its hardcoded behavior is represented by configurable engine and editor systems.

## Guiding Rules

- Every authoring mutation made from the editor must support undo and redo.
- A continuous gesture creates one history entry, not one entry per frame.
- Scene data must be deterministic, versioned, and independent from runtime pointers.
- Runtime resources must be referenced through stable asset identities.
- Authoring features belong in the editor; reusable game capabilities belong in the runtime.
- Editor-only state must never leak into scenes, standalone settings, or player preferences.
- New components must not require unrelated manual edits across Inspector, history, serialization, validation, and cloning.
- Hardcoded demo behavior must not become an implicit engine API.
- Build and focused tests must remain green after each increment.

---

## Foundation Already Established

These systems form the current baseline and should be extended rather than rewritten.

### Viewport and Transform Tools

- [x] Move, Rotate, and Scale gizmos with reversible gestures.
- [x] World and Local transform spaces.
- [x] Configurable position, rotation, and scale snapping.
- [x] Multi-selection alignment and distribution commands.
- [x] Transformed selection geometry shared by outlines, picking, and marquee selection.
- [x] Free-form Scene View camera with cursor-centered exponential zoom and DPI-aware panning.
- [x] Configurable grid, viewport rulers, and draggable guides.
- [x] Compact viewport toolbox and contextual viewport menus.
- [x] Frame Selection, Zoom to Fit, and Reset View shortcuts.

### Camera, Presentation, and Display

- [x] Camera 2D as an editable ECS entity with undo/redo.
- [x] Primary camera selection and stable follow targets.
- [x] Fixed Height, Fixed Width, Expand, and Pixel Perfect projection policies.
- [x] Separation between free-form Scene View and project Game View framing.
- [x] Fit, Fill, Stretch, and Responsive presentation policies.
- [x] Independent Game View render-quality scaling.
- [x] Runtime `DisplayManager` API with desktop window modes, displays, refresh rates, VSync, DPI, orientation, capabilities, and rollback.
- [x] Project display defaults separated from runtime `PlayerPrefs` and editor window state.

### Rendering and Post-Processing

- [x] Global post-processing pipeline with reusable profiles.
- [x] GPU material abstraction shared by sprites and fullscreen passes.
- [x] Validate the Night Vision GPU material on Metal sprites and fullscreen output with deterministic hardware pixel probes.
- [x] Backend-neutral shader variants with an SDL GPU material backend.
- [x] Regression tests for post-process state, history, material uniforms, and source/destination routing.

### Current Automated Baseline

- [x] Camera projection tests.
- [x] Display manager and display persistence tests.
- [x] Post-processing and material tests.
- [x] Editor property transaction and Inspector command tests.
- [x] Clean CMake build and CTest execution.

---

## Phase 1 — Editor Mutation Architecture

**Priority:** Immediate.

**Goal:** Finish Modules A and C on a shared reversible editing foundation before adding more authorable component types.

### Common Property Transactions

- [x] Introduce a reusable `EditorPropertyTransaction` lifecycle: begin, live preview, commit, and cancel.
- [x] Support scalar, integer, boolean, enum, text, color, vector, list, and asset-reference state through typed transactions.
- [x] Guarantee one undo entry per drag, scrub, text commit, toggle, or compound edit.
- [x] Make cancellation restore the exact previous state.
- [x] Add component-specific transaction side-effect hooks, including selective physics and tilemap rebuilds.
- [x] Centralize reusable property validation policies instead of keeping limits inside individual drawers.
- [x] Make the same transaction system work with multi-selection and mixed values.

### Inspector Migration

- [x] Make entity rename reversible.
- [x] Migrate every currently exposed Sprite property to the common transaction lifecycle.
- [x] Migrate every authored RigidBody property to the common transaction lifecycle.
- [x] Migrate every Collider property and shape edit to the common transaction lifecycle.
- [x] Migrate every currently exposed Tilemap setting and destructive action to reversible commands.
- [x] Separate Animator authoring data from editor preview/playback state.
- [x] Migrate Animator clips and spritesheet settings to reversible commands.
- [x] Migrate Transform and Camera properties to the common transaction lifecycle.
- [x] Route texture loading and fallbacks through `AssetManager`; never allocate unmanaged textures from Inspector code.
- [x] Add generic undoable component add/remove commands, while keeping structural Transform non-removable.

### Inspector Structure

- [x] Split the monolithic Inspector into component drawers with a shared compact property layout.
- [x] Keep component UI extensions separate from runtime component definitions.
- [x] Vendor an editor-only Lucide icon catalog and centralize DPI-independent icon rendering through ImGui's font atlas.
- [ ] Migrate remaining editor emojis and ad-hoc symbols to the shared Lucide registry.
- [ ] Add component search, favorites, and concise contextual documentation.
- [ ] Preserve the existing compact Photoshop/Unity-like value scrubbing behavior.

### Sprite Authoring Completion

- [x] Expose RGB tint and independent opacity.
- [x] Expose pivot, Z-depth, flip, visibility, sorting layer, and order in layer.
- [x] Render texture previews in the Sprite Inspector.
- [x] Expose Sprite outline controls through the common property grid and persist their authored state.
- [x] Expose material assignment through registered `AssetHandle` identities in the Sprite Inspector.
- [ ] Make every visual property fully reversible.

**Completion criteria:** Every Inspector and Hierarchy content mutation is reversible, continuous gestures create one command, preview state is not serialized as authoring data, and no Inspector-owned runtime resources leak.

---

## Phase 2 — Component Registry and Scene Data

**Priority:** Immediate after the common property transaction core; establish before adding Light, Canvas, Script, or prefab components.

**Goal:** Stop duplicating component knowledge across snapshots, serialization, cloning, validation, add/remove menus, and editor tooling.

### Component Descriptors

- [x] Introduce an explicit typed component registry without requiring a complex reflection system.
- [x] Register component name, stable serialized key, schema version, and runtime type.
- [x] Register serialize, deserialize, clone, capture, restore, add, remove, and validate operations.
- [x] Add second-phase lifecycle finalization for derived state and entity references.
- [x] Keep built-in component registrations separate from the registry core.
- [x] Allow editor builds to attach component drawers, categories, icons, defaults, dependencies, and incompatibilities without coupling runtime code to ImGui.
- [x] Reject duplicate serialized keys and component-count overflow with explicit diagnostics.

### Serialization and Snapshots

- [x] Migrate `SceneSerializer` component switches to registered serializers for Transform, Sprite, Tag, RigidBody, Collider, Tilemap, Camera, and Animation.
- [x] Migrate serializable `EntitySnapshot` fields to registry-backed component snapshots.
- [x] Preserve stable entity IDs and hierarchy references across save/load and undo/redo.
- [x] Add scene schema versioning and explicit migrations.
- [x] Load scenes transactionally: validate and finalize a candidate before atomically replacing live ECS state, with rollback on commit failure.
- [x] Define authored fields for registered components and exclude animation playback and physics motion state from serialization.
- [x] Add round-trip determinism tests for every currently registered serializable component.
- [x] Add snapshot restore coverage for every currently registered component.

### ECS Scope

- [x] Keep the current ECS storage model unless profiling demonstrates a real limitation.
- [x] Add explicit rejection and diagnostics for invalid component access, component overflow, invalid hierarchy entities, self-parenting, and hierarchy cycles.
- [x] Split component definitions into explicit core, rendering, animation, physics, and particle domain headers; remove the aggregate `Components.hpp` dependency.

**Completion criteria:** Adding a new component requires one registration and its domain implementation, not edits to multiple unrelated switch statements. Scene round trips and command snapshots preserve all authored data.

---

## Phase 3 — Hierarchy, Multi-Selection, and Scene Reliability

**Priority:** High.

**Goal:** Make larger scenes safe to edit before introducing prefabs and scripting.

### Hierarchy Workflow

- [x] Complete true multi-selection editing with mixed-value presentation across shared components (Transform, RigidBody, Collider, Camera).
- [x] Add group/container entities (`GroupComponent`) with reversible `GroupSelectedEntitiesCommand` and `UngroupEntitiesCommand`.
- [x] Preserve local and world transforms correctly during reparenting, grouping, and ungrouping with preflight validation.
- [x] Route Hierarchy drag-and-drop, grouping, and context actions through command history.
- [x] Add reversible hierarchy duplicate, reorder, lock, and visibility commands.
- [x] Add entity lock and editor-only visibility states.
- [x] Detect and reject hierarchy cycles at the `EntityManager` boundary without partially mutating the previous relationship.
- [x] Support multi-drag and atomic bulk reparenting (`ReparentEntitiesCommand`) in Scene Hierarchy.

### Dirty State and Saving

- [x] Derive scene dirty state from committed authoring commands and revision tracking.
- [x] Add Save, Save As, and per-scene editorial sessions.
- [x] Add Save All and recent scenes.
- [x] Keep loaded scenes in viewport tabs so switching preserves each scene session without a save prompt.
- [x] Warn before loading, switching, closing, or entering destructive workflows with unsaved changes, featuring transactional discard rollback.
- [x] Add configurable autosave and crash/interruption recovery.
- [x] Keep editor layout, snapping, guides, and view preferences separate from scene content.

### Validation and Diagnostics

- [x] Add a Scene Validator for missing assets, invalid colliders, absent or duplicate primary cameras, hierarchy errors, and invalid tilemaps.
- [x] Add a diagnostics panel with entity/asset navigation and safe fix actions.
- [x] Make every automatic fix reversible when it changes authored content.
- [ ] Add tests for history invalidation, scene switching, hierarchy commands, and recovery.

**Completion criteria:** Complex scenes can be edited, reorganized, recovered, validated, and saved without losing authoring state or producing stale history entries.

---

## Phase 4 — Asset Pipeline and Prefabs

**Architecture gate:** Stable asset identities must exist before prefab serialization and dependency-aware reload.

**Goal:** Replace provisional paths and runtime pointers with reusable, inspectable project assets.

### Asset Database

- [x] Introduce stable `AssetHandle` identities and project-relative source paths (`AssetDatabase`, atomic `.meta` sidecars, project-root resolver, duplicate-handle rejection).
- [x] Persist prefab links and prefab-variant bases as `AssetHandle`; resolve their runtime paths only through `AssetDatabase` (initial format, with no legacy migration path).
- [x] Persist Sprite texture references as `AssetHandle`; resolve ready textures through `AssetDatabase` and fall back safely when unavailable.
- [x] Persist Sprite material references as `AssetHandle`; load validated `.material` assets through `AssetDatabase` and fall back safely when unavailable.
- [x] Make components serialize asset handles instead of runtime pointers.
- [x] Resolve handles to runtime resources through the asset system.
- [x] Add explicit missing, loading, failed, and ready states with safe fallback assets.
- [x] Add metadata and import settings for filtering, pixels per unit, pivot, compression, and platform overrides.
- [x] Add material dependency tracking and dependency-safe reload.
  - [x] Track material-to-shader-to-source dependencies and propagate missing-source state safely.
- [x] Add drag-and-drop from Asset Browser to Viewport and Hierarchy.

### Materials and Shaders as Assets

- [x] Register materials and post-process profiles as project assets; create initial `.material` and `.postprocess` files from Asset Browser.
- [x] Select, load, and atomically save `.postprocess` assets from Post-Processing Tuner.
- [x] Resolve spatial post-process volumes through `PostProcessProfile` asset handles with a safe base-profile fallback.
- [x] Recreate global post-process presets as `.postprocess` assets and select them from the Tuner.
- [x] Add an offline shader compilation/validation pipeline for Metal, Vulkan, and Direct3D variants (pinned SDL_shadercross target generates and structurally validates MSL, SPIR-V, and DXIL build artifacts).
- [x] Stage runtime assets with the platform-selected Night Vision shader variant while preserving asset-handle identities.
- [x] Report unavailable shader variants before runtime (the Material editor shows current-renderer support and Sprite Inspector blocks incompatible assignments).
- [x] Add material parameter editing, serialization, duplication, and missing-shader diagnostics in Asset Browser.

### Prefabs

- [x] Add prefab assets backed by registry-based entity/component serialization (`EntitySubtreeArchive`, `PrefabSerializer`).
- [x] Add isolated Prefab Stage edit mode (`PrefabStageController`, transient scene, isolated command history, breadcrumb navigation, dirty protection).
- [x] Add instance linkage (`PrefabLinkComponent`, `[P]` cyan visual cues in Hierarchy, asset path tooltips).
- [x] Add Apply, Revert Contents, Unpack, and batch multi-selection actions (`ApplyPrefabCommand`, `RevertPrefabCommand`, `UnpackPrefabCommand`).
- [x] Add Live Asset Propagation (`PrefabPropagation::propagate_to_instances`) from Prefab Stage save and Inspector Apply to all active scene instances.
- [x] Add duplication link preservation (`DuplicateEntitiesCommand` clones preserve independent `PrefabLinkComponent`).
- [x] Make prefab operations fully reversible, transactional, and dependency-safe without disk dependencies on undo/redo.
- [x] Add explicit per-property/component override tracking & granular revert/apply visuals (3px blue gutter, context menus, and Overrides dropdown popup) in the Inspector.

- [x] Add nested-prefab policies and Prefab Variants (`has_prefab_nesting_cycle`, `base_prefab_path`, `[V]` badge, `CreatePrefabVariantCommand`).


**Completion criteria:** Assets and prefabs remain valid across reload, move, duplication, save/load, and standalone packaging without runtime-pointer fragility.


---

## Phase 5 — Render Architecture for Advanced Systems

**Architecture gate:** Introduce explicit render stages before local multipass effects, lighting, shadows, and Canvas composition create special-case render paths.

**Goal:** Extend the current renderer rather than replace it, while making ordering and intermediate surfaces explicit.

- [x] Define render stages for scene geometry, local effect surfaces, lighting, global post-processing, Canvas/UI, and final presentation.
  - [x] Establish the explicit `SceneGeometry -> LocalEffects -> Lighting -> GlobalPostProcess -> CanvasUI -> Presentation` contract in `Renderer2D`, including per-stage frame metrics.
  - [x] Add the pooled local-effects stage with ordered pass chaining and automatic intermediate-surface release.
  - [x] Add the lighting stage with reusable compositing passes before global post-processing.
- [x] Introduce render layers shared by existing renderables and cameras.
  - [x] Add common layer identities and camera masks for sprites, tilemaps, and particles.
  - [x] Make render layers stable composition groups while preserving authored order within each layer.
  - [x] Expose authored layer and camera-mask controls in the Inspector.
- [x] Support entity- or layer-scoped multipass effects through managed intermediate surfaces.
  - [x] Add reusable `.layereffect` recipes with ordered material passes plus scene-owned layer assignments, cached runtime resolution, authoring, and undo/redo.
  - [x] Add entity-scoped multipass isolation and authoring through a serializable `RenderEffectComponent`, reusable profile assets, pooled composition surfaces, missing-asset diagnostics, and undo/redo.
  - [x] Extract profile resolution and runtime pass caching from `GenericScene` into the reusable `RenderEffectSystem`.
- [x] Keep simple single-pass sprite materials on the direct fast path.
- [x] Define clear ownership and pooling for render targets.
- [x] Track draw calls, material switches, pass timings, and render-target memory.
  - [x] Report per-stage draw calls, material switches, and CPU timings; expose render-target pool allocation metrics.
- [x] Remove obsolete renderer compatibility stubs after all callers use the current batching path.
- [x] Add GPU integration tests or deterministic validation scenes for pass ordering and state reset.
  - [x] Add mode 9 to `PostProcessValidationScene` for deterministic entity, layer, global post-process, and HUD composition using the reusable `ManualNightVision` asset; validate probes and render-target release on Metal.

**Completion criteria:** New effects and lighting can be composed through explicit stages without embedding feature-specific branches throughout `Renderer2D` or scene code.

---

## Phase 6 — Runtime Authoring Systems

**Goal:** Expose core game presentation systems through registered, serializable components.

### Lighting

- [ ] Implement ambient and point lights with color, radius, intensity, and blend mode.
- [ ] Apply shared render-layer identities and camera masks to Light components.
- [ ] Add Light components, Inspector drawers, viewport gizmos, serialization, undo/redo, and validation.
- [ ] Implement 2D shadow casters from colliders and static geometry.

### Canvas and Responsive UI

- [ ] Add Canvas, text, image, and button components.
- [ ] Add anchors, pivots, responsive layout, scaling policy, and input routing.
- [ ] Add safe-area and notch support.
- [ ] Add UI authoring tools, serialization, undo/redo, and validation.

### Display and Mobile Follow-Up

- [ ] Add standalone render scale and dynamic resolution.
- [ ] Add configurable orientation policies.
- [ ] Handle mobile lifecycle, suspend/resume, surface recreation, rotation, and split-screen changes.
- [ ] Add real iOS and Android targets before claiming mobile support.

**Completion criteria:** Lighting and responsive UI can be authored visually, survive display changes, and serialize without platform-specific game code.

---

## Phase 7 — Scripting and Play Mode

**Goal:** Enable rapid gameplay iteration without recompiling C++ or compromising authoring state.

- [ ] Add Lua runtime integration and a registered `ScriptComponent`.
- [ ] Bind entities, transforms, input, audio, physics, events, assets, display, and scene loading.
- [ ] Add script creation, attachment, field inspection, editing, and error reporting.
- [ ] Add hot reload with explicit lifecycle and failure behavior.
- [ ] Preserve the current safe default: Stop restores the authoring snapshot.
- [ ] Add opt-in Play Mode apply-back for explicitly selected properties.
- [ ] Keep runtime script state distinct from serialized public fields.

**Completion criteria:** Gameplay behavior can be authored, inspected, debugged, and reloaded while Edit and Play state remain predictably isolated.

---

## Phase 8 — Debugging and Performance

**Goal:** Make correctness and performance problems visible before export.

- [ ] Add physics overlays for colliders, contacts, velocities, centers of mass, and collision layers.
- [ ] Add entity-scoped logs and script error locations.
- [ ] Add frame profiling for update, render, physics, editor UI, memory, draw calls, culling, and GPU passes.
- [ ] Add pause, step frame, timescale, and runtime entity inspection.
- [ ] Add stress scenes and budgets for entity count, sprites, physics bodies, particles, and render targets.
- [ ] Move large header implementations into source files when compile time or ownership boundaries justify it.
- [ ] Optimize only after profiling identifies a measurable bottleneck.

**Completion criteria:** Developers can identify which system, entity, asset, or render pass causes a correctness or performance problem.

---

## Phase 9 — Runtime/Editor Separation and Export

**Architecture gate:** Runtime, editor, demos, and game code must become separate build targets before standalone packaging is considered complete.

**Goal:** Produce self-contained desktop games without linking or initializing editor-only systems.

- [x] Split CMake targets into runtime library, editor application, sandbox/demos, tests, and standalone game.
- [x] Move editor panel construction out of the runtime `Application` path.
- [x] Ensure standalone startup never initializes ImGui, editor panels, or editor history.
- [x] Replace runtime source duplication in test targets with the reusable `EngineRuntime` library.
- [x] Package required scenes, assets, shader variants, project settings, and native dependencies for the macOS standalone bundle.
- [ ] Create platform-native distributable layouts: `.app` bundle on macOS, portable folder/ZIP or installer on Windows, and AppImage/tarball on Linux.
- [x] On macOS, copy non-system `.dylib` dependencies into `Engine2D.app/Contents/Frameworks`, rewrite their load paths with `install_name_tool`, and codesign the finished bundle. The exported app must not depend on Homebrew paths such as `/opt/homebrew/opt/...`.
- [x] Add a Windows portable-folder packaging target that collects non-system runtime DLLs and validate it on Windows.
- [x] Add verification and ZIP packaging targets for the Windows x64 portable export.
- [x] Add a macOS-to-Windows x64 MinGW cross-build toolchain backed by local vcpkg dependencies and validate its output on Windows.
- [ ] On Linux, bundle or document compatible shared-library requirements without relying on development-machine paths.
- [x] Build an ad-hoc signed universal macOS bundle with arm64 and x86_64 executable slices.
- [x] Add export configuration validation and actionable build logs for macOS.
- [x] Add an isolated macOS export smoke test that executes the packaged standalone resource checks.
- [ ] Implement standalone builds for macOS, Windows, and Linux.
- [ ] Add clean-machine smoke tests for exported builds, including macOS machines without Homebrew/SDL installed.

**Completion criteria:** A standalone build contains only runtime and game dependencies, launches with project defaults, resolves every packaged asset, and does not depend on editor state.

---

## Required Test Suites by Module

Tests are part of each module's completion criteria. They must be added with the behavior they protect, not postponed until the debugging or export phases. Pure logic should run headlessly in CTest; SDL, GPU, window, audio, and filesystem behavior should use injected fake backends where practical and retain a smaller real-platform smoke layer.

### Existing Suites

- [x] `Camera2DTests` — projection policies, reference framing, zoom, and coordinate conversion.
- [x] `DisplayManagerTests` — capabilities, modes, atomic apply, rollback, hot-plug, DPI, and mobile-style read-only behavior.
- [x] `DisplayPersistenceTests` — separation between project defaults, runtime preferences, and editor window state.
- [x] `PostProcessTests` — profile state, command history, material uniforms, source/destination routing, and shader/material validation.

### Existing-System Regression Backlog

These suites protect systems that already exist but do not yet have dedicated automated coverage.

- [x] `EntityManagerTests` — entity reuse, stable-ID restoration, component lifecycle callbacks, cloning, filtered views, invalid access, reciprocal hierarchy links, and cycle rejection.
- [ ] `TransformHierarchyTests` — local/world composition, rotated/scaled parents, visibility propagation, reparenting, cycle rejection, and stable child transforms.
- [ ] `ViewportGeometryTests` — transformed bounds, point picking, marquee intersection, strict marquee, pivots, colliders, and child entities.
- [ ] `ViewportCameraControllerTests` — exponential zoom, cursor anchoring, limits, pan scaling, focus rules, and DPI independence.
- [ ] `TransformGizmoTests` — Move/Rotate/Scale deltas, World/Local axes, snapping, multi-selection, child transforms, and one-command-per-gesture behavior.
- [ ] `Renderer2DTests` — sorting layer/order/depth stability, batching boundaries, material state reset, flips, pivots, source rectangles, and render-target routing.
- [ ] `PhysicsSystemTests` — body creation/destruction, synchronization, collision filters, triggers, one-way collisions, fixed rotation, and transform ownership.
- [ ] `TilemapTests` — sparse tile storage, painting commands, bounds, serialization, composite collider rebuild, and generated-entity cleanup.
- [x] `AnimationSystemTests` — frame selection, looping, non-loop completion, custom frames, spacing/margins, and authored/runtime state separation.
- [ ] `InputRoutingTests` — Edit/Play isolation, text-input capture, viewport focus/hover, mouse-coordinate conversion, and keyboard state transitions.
- [ ] `SaveSystemTests` — binary/JSON round trips, version rejection, corruption handling, atomic replacement, and failure recovery.
- [ ] `AssetManagerLifetimeTests` — cache identity, fallback ownership, clear/reload behavior, duplicate loads, and no unmanaged Inspector resources.

### Phase 1 — Editor Mutation Suites

- [x] `EditorCommandHistoryTests` — execute, undo, redo, redo invalidation, bounded history, scene invalidation, and command descriptions.
- [x] `EditorPropertyTransactionTests` — begin/preview/commit/cancel, unchanged edits, compound values, text commits, and one history entry per gesture.
- [x] `InspectorCommandTests` — entity rename, Sprite, Animator, Transform and Camera edits, primary-camera state, authored/runtime separation, component presence, exact restoration, physics fixture rebuild, and Tilemap lifecycle.
- [ ] `MultiEditTransactionTests` — mixed values, partial component availability, atomic multi-entity commit, cancel, undo, and redo.

### Phase 2 — Component and Serialization Suites

- [x] `ComponentRegistryTests` — runtime/editor registration, duplicate keys and drawers, schema versions, lifecycle hooks, dependencies, incompatibilities, complete add/remove, clone, capture/restore, validation, and component-limit diagnostics.
- [x] `SceneSerializerTests` — deterministic output, component round trips, stable IDs, hierarchy references, malformed data, transactional rejection, and legacy loading.
- [ ] `EntitySnapshotTests` — delete/restore, duplicate, component completeness, hierarchy restoration, asset references, and runtime-only field exclusion.
- [x] `SceneMigrationTests` — every supported historical schema migrates forward or fails with an actionable error.

### Phase 3 — Hierarchy and Reliability Suites

- [x] `EditorEntityStateStoreTests` — lock, visibility, selection isolation, and command synchronization.
- [x] `ViewportVisibilityTests` — ancestor propagation, picking rejection, rendering filtering, and gizmo exclusion.
- [x] `HierarchyCommandTests` — duplicate, group, ungroup, reparent, reorder, multi-drag, lock rejection, cycle prevention, local/world preservation, undo, and redo.
- [ ] `SceneLifecycleTests` — dirty state, save, Save As, Save All, switch/close prompts, Play snapshot restoration, and stale-history prevention.
- [x] `AutosaveRecoveryTests` — atomic autosave discovery, crash recovery records, external-source conflict detection, and cleanup after successful save. Interval preference behavior is exercised through the editor integration build.
- [ ] `SceneValidatorTests` — missing assets, invalid colliders/tilemaps, camera rules, hierarchy errors, diagnostics navigation, and reversible fixes.

### Phase 4 — Asset and Prefab Suites

- [x] `AssetDatabaseTests` — stable handles, project-relative resolution, duplicate metadata rejection, deterministic discovery, and atomic metadata generation.
- [x] Extend `AssetDatabaseTests` — path moves, missing assets, dependency graphs, reload, and failure recovery.
- [x] `AssetImportTests` — central import settings, platform overrides, metadata persistence, path moves, reload, and missing-source recovery are covered by `AssetDatabaseTests`.
- [x] `ShaderAssetTests` — variant discovery, dependency invalidation, backend preflight, and actionable diagnostics are covered by `ComponentRegistryTests` and shader build validation.
- [x] `MaterialAssetTests` — parameter serialization, defaults, duplication, missing shaders, dependency reload, and sprite compatibility are covered by `ComponentRegistryTests` and `PrefabTests`.
- [x] `PrefabTests` — creation, instantiation, overrides, Apply, Revert, Unpack, nesting policy, dependency reload, undo, and scene round trips.

### Phase 5 — Render Architecture Suites

- [x] `RenderPipelineTests` — entity/layer/global/HUD ordering, layer filtering, intermediate-surface lifetime, pooling, resize, state reset, and direct fast-path selection.
- [x] `LocalEffectsTests` — per-entity and per-layer isolation, multipass ordering and failure cleanup, transformed sprites, alpha composition, and pool reuse.
- [x] `RenderMetricsTests` — draw calls, material switches, pass timings, surface memory, pool reuse, and stable counter reset per frame.
- [x] `GPURenderSmokeTests` — hardware-labeled native shader execution with deterministic sprite and fullscreen pixel probes; skip unsupported devices without failing headless CI.
  - [x] Validate Metal locally and validate staged MSL, DXIL, and SPIR-V descriptor formats, entry points, and asset paths headlessly.

### Phase 6 — Runtime Authoring Suites

- [ ] `Lighting2DTests` — light bounds, intensity/color, layer masks, blending, shadow-caster geometry, serialization, and command restoration.
- [ ] `CanvasLayoutTests` — anchors, pivots, nested layout, reference scaling, Fit/Fill/Stretch/Responsive behavior, and safe areas.
- [ ] `UIInputTests` — hit testing, focus, navigation, pointer capture, viewport conversion, and overlap ordering.
- [ ] `MobileSurfaceTests` — orientation, safe-area updates, surface recreation, suspend/resume, split-screen resize, and capability restrictions using a fake backend.
- [ ] Real-device iOS and Android smoke tests before mobile support is declared complete.

### Phase 7 — Scripting and Play Mode Suites

- [ ] `LuaRuntimeTests` — VM lifecycle, component bindings, entity validity, errors, events, assets, display API, and deterministic teardown.
- [ ] `ScriptHotReloadTests` — successful reload, syntax/runtime failure, state policy, removed fields, and recovery without corrupting the scene.
- [ ] `ScriptSerializationTests` — public fields, defaults, type changes, missing scripts, asset/entity references, and schema evolution.
- [ ] `PlayModeIsolationTests` — authoring snapshot restoration, runtime-only mutations, explicit apply-back, script state, physics state, and command-history isolation.

### Phases 8 and 9 — Performance and Export Suites

- [ ] `ProfilerInstrumentationTests` — counter scope, nested timings, frame reset, disabled overhead, and entity/pass attribution.
- [ ] `StressRegressionTests` — recorded budgets for entities, sprites, physics, particles, scripts, and render-target allocation.
- [ ] `RuntimeBoundaryTests` — runtime targets do not link or initialize ImGui, editor panels, command history, or sandbox helpers.
- [ ] `AssetPackagingTests` — dependency closure, shader variants, scene inclusion, missing-file failure, and deterministic package manifests.
- [ ] `StandaloneSmokeTests` — launch, load initial scene, input, audio initialization, display defaults, save path, and clean shutdown.
- [ ] Export smoke coverage on macOS, Windows, and Linux, including a clean environment without source-tree assets.

### Continuous Test Gates

- [ ] Every bug fix receives a regression test at the lowest practical layer.
- [ ] Every new serializable component receives registry, round-trip, snapshot, validation, and command tests.
- [ ] Every new platform backend receives a fake-backend contract suite plus real-platform smoke validation.
- [x] CTest remains headless and deterministic by default; hardware-dependent suites are opt-in and labeled separately.
- [ ] CI runs formatting/static checks, build, unit tests, integration tests, and the applicable platform smoke tests.
- [ ] Test fixtures never write into project scenes, project settings, `PlayerPrefs`, or editor layout state unless isolation is the behavior under test.

---

## Deferred Viewport Polish

These items are useful but should not interrupt the architecture work above unless required by an active workflow.

- [ ] Optional safe-area overlays in Scene View.
- [ ] Guide naming, locking, visibility groups, and persistence policies.
- [ ] Additional snapping targets such as vertices, edges, pivots, and tile boundaries.
- [ ] Customizable toolbox composition and multiple reusable editor toolboxes.
- [ ] Platform-specific DPI visual checks and final icon optical alignment.

---

## Execution Order

1. Phase 1 — Editor Mutation Architecture.
2. Phase 2 — Component Registry and Scene Data.
3. Phase 3 — Hierarchy, Multi-Selection, and Scene Reliability.
4. Phase 4 — Asset Pipeline and Prefabs.
5. Phase 5 — Render Architecture for Advanced Systems.
6. Phase 6 — Runtime Authoring Systems.
7. Phase 7 — Scripting and Play Mode.
8. Phase 8 — Debugging and Performance.
9. Phase 9 — Runtime/Editor Separation and Export.

## Next Concrete Increments

1. **Scene Validator Foundations (Phase 3)**:
   - [x] Introduce a pure validation result model for absent or duplicate primary cameras and hierarchy errors. It does not mutate authored scene state.
   - [x] Add deterministic tests for each diagnostic and for valid scenes.
   - [x] Extend the model with missing assets and invalid collider/tilemap diagnostics.
2. **Diagnostics Panel and Navigation (Phase 3)**:
   - [x] Present grouped validation results, select the related entity, and refresh after scene edits without changing authored content.
   - [x] Add asset navigation for diagnostics with a resolvable asset path.
   - [x] Expose unresolved asset handles and allow a confirmed, reversible cleanup.
3. **Reversible Safe Fixes (Phase 3)**:
   - [x] Remove nonexistent child references through a command-backed fix with Undo/Redo.
   - [x] Add fixes only where the correction is unambiguous; every authored mutation supports Undo/Redo.
4. **Autosave and Recovery Foundations (Phase 3)**:
   - [x] Write dirty scene snapshots atomically outside authored scene files and discover them on the next editor launch.
   - [x] Require an explicit Recover or Discard choice; recovered content remains dirty until normally saved or discarded.
   - [x] Expose the interval and recovery storage policy in Editor Preferences, then add conflict handling for source files changed outside the editor.

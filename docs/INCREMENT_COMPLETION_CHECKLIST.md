# Increment Completion Checklist

Use this checklist when closing an editor, engine, runtime, or tooling increment. It is a review aid: not every item applies to every change, but any applicable item must be consciously verified or documented as deferred.

## 1. Scope and ownership

- [ ] The change belongs to the correct layer: engine runtime, editor-only tooling, project settings, or game code.
- [ ] Editor-only code, ImGui state, commands, and diagnostics do not leak into the standalone runtime path.
- [ ] The public contract is clear: inputs, outputs, ownership, failure behavior, and threading assumptions are explicit.
- [ ] Existing approved behavior is preserved unless the increment explicitly changes it.
- [ ] Scene data remains independent from runtime, renderer, GPU-resource, and editor-view state.

### 2. Undo, redo, cancellation, and dirty state

- [ ] Every persistent mutation initiated by the editor is represented by one atomic `IEditorCommand`.
- [ ] A continuous gesture creates one history entry on commit, not one per frame or per intermediate value.
- [ ] Undo restores the exact prior state; redo reapplies the exact committed state.
- [ ] Redo is as strictly transactional as execute: if snapshot restoration, reparenting, or component finalization fails midway during redo, it must cleanly roll back all restored entities/state, restore the previous selection, and return false rather than leaving a partial or corrupted state.
- [ ] Escape/cancel restores the initial state immediately and does not create a history entry.
- [ ] Commands that can fail are atomic: they leave no partially modified entities, hierarchy, physics state, or selection.
- [ ] Multi-entity and subtree operations pre-validate all locks and constraints across all affected nodes before performing mutations.
- [ ] Saving marks the correct history revision as clean; undoing back to it removes the dirty state.
- [ ] View-only state (selection, pan, zoom, grid visibility, layout) does not enter undo/redo or dirty state.

## 3. Hierarchy and transforms

- [ ] All authored transform edits preserve world-space intent, then convert to local space using the actual parent world transform.
- [ ] Duplication and creation of entities under an unselected parent compute the target in world space and convert to local space using `world_to_local_transform` to honor rotated, scaled, or non-uniform parent coordinate frames.
- [ ] Descendant entities in duplicated subtrees preserve their relative local transforms and hierarchy topology so world-space geometry remains intact.
- [ ] Selected descendants are filtered when their selected ancestor already drives the subtree.
- [ ] Parent changes, grouping, ungrouping, and snapshot restore preserve world transforms unless the operation explicitly says otherwise.
- [ ] World transforms are current before they are used for centroids, bounds, picking, camera framing, or physics sync.
- [ ] Negative scale / mirror state is preserved when scaling; do not silently force all scale components positive.
- [ ] Degenerate transforms and invalid parents fail safely and restore the original state.

## 4. Multi-selection and mixed values

- [ ] The operation applies only to eligible entities (alive, component present, editable, unlocked, and valid for the domain).
- [ ] The same filtered set is used for UI presentation, preview, commit, undo, redo, and side effects.
- [ ] Mixed fields display a neutral value such as `---` and preserve every untouched axis/property per entity.
- [ ] A bulk edit changes only the field the user actually changed; it must not overwrite unrelated mixed values.
- [ ] Entity pickers filter invalid targets (self, selected entities when inappropriate, incompatible component types, invalid hierarchy links).
- [ ] Empty or fully ineligible selections are safe no-ops with understandable UI feedback.

## 5. Runtime and derived state

- [ ] The authoritative data model is modified first; derived state is synchronized from it.
- [ ] Expensive work (physics fixture rebuilds, texture rebuilds, serialization, shader/material recompilation) happens once at commit, undo, redo, or a deliberate debounce point—not every UI frame.
- [ ] Lightweight preview synchronization is sufficient during drag/edit and is explicitly separated from expensive rebuilds.
- [ ] Runtime-only settings do not silently persist to project configuration or editor history.
- [ ] Project defaults, editor preferences, and game/player preferences have separate ownership and persistence paths.

## 6. Design and scalability

- [ ] No new behavior depends on entity names, demo scene contents, magic IDs, screen resolution, or fixed hardcoded limits unless clearly documented as temporary integration code.
- [ ] Presentation fallbacks (e.g. default framing half-sizes, padding, snap thresholds) use explicitly named constants or style tokens rather than hardcoded magic numbers.
- [ ] Repeated geometry, coordinate conversion, filtering, or transaction logic is extracted into a shared helper with one source of truth.
- [ ] Pure math/data logic is separated from ImGui and SDL so it can be tested directly.
- [ ] The abstraction eliminates a concrete coupling or duplication; it is not added only to reduce line count.
- [ ] Hot paths avoid per-frame allocations, unnecessary rebuilds, broad ECS scans, or logging noise.
- [ ] If the increment exposes a limitation that will not scale, record the trade-off and update the roadmap instead of hiding it behind another local patch.

## 7. UI and interaction

- [ ] Keyboard shortcuts only act when their intended panel has focus (or hover when intended) and no text widget is being edited (`!io.WantTextInput`).
- [ ] Mouse coordinates, drag deltas, and viewport interactions are DPI-aware.
- [ ] Disabled, locked, mixed, unsupported, and unavailable states are distinguishable without relying solely on color.
- [ ] Context menus reflect the clicked context rather than exposing unrelated global commands.
- [ ] Visual previews (gizmos, outlines, collider debug, grids) use the same geometry/model as interaction where possible.
- [ ] Tooltips, labels, icon alignment, and hit targets are verified at the editor's supported DPI scales.

## 8. Tests and validation

- [ ] Tests exercise production helpers or commands—not duplicated copies of the implementation math.
- [ ] Tests cover the successful path, expected rejection, cancellation/rollback, boundaries, and the regression that motivated the change.
- [ ] For commands: test execute, undo, redo, and history branching when relevant.
- [ ] For hierarchy/transform work: test transformed parents (translation, rotation, non-uniform scale), selected parent plus child, locks, and degenerate cases.
- [ ] For rendering/platform work: add runtime diagnostics and a concrete manual validation protocol when hardware behavior cannot be unit tested.
- [ ] Run focused tests while developing.
- [ ] Run `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `git diff --check` before handoff when applicable.

## 9. Repository hygiene and delivery

- [ ] Inspect `git status` before editing and before delivery.
- [ ] Preserve unrelated local work; in particular, never include `imgui.ini` unless explicitly requested.
- [ ] Update the active roadmap when work is completed, re-scoped, or newly discovered.
- [ ] Report observable behavior, architecture decisions, automated validation, manual checks still required, and known limitations.
- [ ] Commit only when explicitly requested, and keep it scoped to the increment.

## Closure note template

Use this short structure for every completed increment:

1. **Result:** what now works for the user.
2. **Architecture:** key ownership, shared helper, or contract decision.
3. **Safety:** undo/cancel/rollback and derived-state behavior.
4. **Validation:** tests and commands run; manual checks required.
5. **Follow-up:** roadmap item, limitation, or deliberate deferral.

# Debugging and Performance Plan

**Status:** Planned and intentionally deferred.

This document defines the implementation path for Roadmap Phase 8. It is a
handoff contract for a future module, not a claim that the features below are
already implemented.

## Goals

- Make runtime correctness problems attributable to a system, entity, asset,
  script source location, or render pass.
- Make frame cost visible before attempting optimization.
- Keep debugging controls and visualizations generic and independent from game
  content or a concrete scene implementation.
- Preserve the runtime/editor build boundary: runtime targets collect neutral
  diagnostic data, while editor targets own panels, overlays, and interaction.
- Keep instrumentation disabled or inexpensive when it is not being observed.

## Non-goals

- An embedded script editor or source debugger.
- Remote telemetry, crash reporting, or cloud profiling.
- Automatic optimization based on profiler results.
- Gameplay-specific debug views or stress content in the engine core.
- Shipping editor panels or ImGui with standalone games.

## Architecture rules

1. Runtime systems expose immutable frame snapshots or event records. Editor
   panels must not reach into Box2D, renderer internals, or EngineScript runtime state.
2. Entity IDs, asset handles, component keys, render-pass names, and project-
   relative source paths are the stable attribution keys.
3. Pause, step, timescale, filters, overlay visibility, and profiler capture are
   ephemeral session state. They do not dirty scenes or enter authoring history.
4. Runtime entity edits remain outside this phase. Inspection is read-only
   until a separate reversible live-edit contract is approved.
5. No optimization is accepted without a captured baseline, a named budget,
   and a repeatable measurement demonstrating the improvement.
6. Tests use deterministic counters and fake clocks where possible. Hardware
   GPU timing and real-device performance remain explicitly separate checks.

## Increment 1: Structured diagnostics

Introduce a structured diagnostic record shared by engine producers and editor
consumers. A record should carry severity, category, message, timestamp, and
optional entity, asset, source path, line, column, system, and render-pass
attribution.

The existing logger remains the single output path. `ConsolePanel` gains filters
for severity, category, entity, asset, and free text without parsing formatted
strings. Its retained record buffer must be bounded.

EngineScript compilation and runtime errors should preserve the project-relative
source name and exact line/column. `ScriptInspectorDrawer`
links the diagnostic to the owning entity and script asset; opening the source
delegates to the operating system or configured external editor.

Validation:

- Exact EngineScript source attribution for compile, module-load, lifecycle, update, and
  event callback failures.
- Stable filtering when messages contain similar text.
- Bounded retention and deterministic ordering.
- Standalone logging works without linking editor code.

## Increment 2: Physics debug snapshots and overlays

Extend the existing viewport collider visualization through a neutral
`PhysicsDebugFrame` produced by the physics layer. Each frame contains only the
data required for drawing:

- collider geometry and trigger/one-way state;
- active contacts, contact points, and normals;
- linear and angular velocity vectors;
- body origin and center of mass;
- body type, collision category, and mask.

The editor renders this snapshot through one viewport overlay with independent
toggles and a small legend. It must use the same world-to-viewport conversion as
selection and lighting tools, remain correct under camera movement and zoom,
and display the last captured state while paused. Collection must not step or
mutate the physics world.

Validation:

- Box, circle, polygon, trigger, and one-way collider geometry.
- Contact-normal orientation for both participants.
- Camera zoom, pan, game resolution, and high-DPI mapping.
- Layer/category colors and filters.
- No overlay rendering or collection cost when disabled.

## Increment 3: Runtime transport and inspection

Centralize simulation transport in `SceneManager`:

- Play, Pause, Resume, and Stop retain their current isolation contract.
- Step advances exactly one fixed simulation frame while remaining paused.
- Timescale is explicit, clamped, and reset on Stop.
- Pausing does not accumulate a resume-frame delta spike.

Add a read-only runtime entity mode to the hierarchy and Inspector. It follows
the active runtime scene, clearly labels runtime-only state, and returns to the
authoring selection after Stop. Runtime scene loading and entity destruction
must invalidate stale runtime selections without affecting authoring history.

Validation:

- Exactly one physics/script/update dispatch per step.
- No audio, input, timer, or event accumulation while paused.
- Runtime selection survives ordinary component mutation and clears safely when
  its entity or scene disappears.
- Stop still restores authoring state and preserves existing Undo/Redo entries.

## Increment 4: Frame profiler

Add a runtime-owned profiler with nested CPU scopes, counters, and a bounded
frame ring buffer. Initial attribution points are:

- frame, fixed update, variable update, scripts, physics, and event dispatch;
- render submission, culling, batching, lighting, post-processing, UI, and
  editor UI;
- draw calls, submitted/culled primitives, active bodies, contacts, scripts,
  particles, render-target allocations, and estimated owned memory;
- individual GPU passes when the backend supports timestamp queries.

The editor presents a frame summary and timeline in `StatsPanel`, including
average, minimum, maximum, and percentile views over a selectable window.
Unsupported GPU metrics must show as unavailable rather than zero. Captures may
be exported as deterministic JSON for regression analysis.

Validation is owned by `ProfilerInstrumentationTests` and covers scope nesting,
frame reset, counter attribution, ring-buffer bounds, disabled overhead, and
unsupported backend behavior.

## Increment 5: Stress regression and budgets

Create generic stress fixtures configured by data rather than hardcoded game
semantics. Scenarios cover entity iteration, sprites and batches, physics bodies
and contacts, particles, scripts, lighting, post-process passes, and render-
target reuse.

Each scenario records correctness budgets first: allocation counts, target-pool
growth, draw-call ceilings, entity/pass attribution, and leak-free teardown.
Wall-clock and GPU budgets are recorded by platform/profile and do not fail
portable CI until a stable baseline exists.

`StressRegressionTests` owns deterministic budgets. Hardware performance runs
remain opt-in and publish their machine, backend, resolution, build type, and
sample window with the result.

## Increment 6: Evidence-driven refactoring

Only after profiler and stress captures identify a measurable bottleneck:

1. Record the baseline and responsible scope/counter.
2. Isolate the ownership or compile-time problem.
3. Apply one focused optimization or move a justified large header
   implementation into a source file.
4. Re-run the same capture and correctness suite.
5. Keep the change only when the result improves without shifting unmeasured
   cost elsewhere.

## Delivery order and gates

| Order | Deliverable | Gate to continue |
| --- | --- | --- |
| 1 | Structured diagnostics and EngineScript locations | Errors are filterable and attributable without string parsing |
| 2 | Physics debug frame and viewport overlay | Overlay is camera-correct, read-only, and zero-cost when disabled |
| 3 | Pause, step, timescale, runtime inspection | Deterministic stepping preserves Play Mode isolation |
| 4 | CPU/counter profiler and Stats UI | Frame scopes and counters pass instrumentation tests |
| 5 | GPU timing where supported | Unsupported backends degrade explicitly |
| 6 | Stress fixtures and budgets | Repeatable baselines exist before optimization |
| 7 | Targeted optimization/refactoring | Same capture demonstrates a measurable improvement |

Each increment requires a focused build and test, followed by the full CTest
suite and `git diff --check`. The roadmap checkbox is marked complete only when
its runtime contract, editor presentation, serialization implications, and
automated coverage are all finished.

## Phase completion criteria

Phase 8 is complete when a developer can pause on a problematic frame, inspect
the relevant runtime entity, see physics and script diagnostics in context, and
attribute CPU, GPU, memory, draw-call, culling, and allocation cost to the system
or pass responsible. Stress baselines must then detect regressions without
requiring gameplay code in the engine repository.

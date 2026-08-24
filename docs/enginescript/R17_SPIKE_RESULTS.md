# Mandatory Platformer Spike Results (Milestone R17)

## 1. Executive Summary

The local Platformer Spike validates the core EngineScript architecture. Native
generated-code compilation and Play Mode execution of authored TypedIR are
covered automatically. Cross-platform packaging and performance measurements
remain open and are not claimed by this document.

---

## 2. Tested Dimensions & Results

| Dimension | Specification Requirement | Result | Status |
| --- | --- | --- | --- |
| **Behaviour Lifecycle** | Synchronous `update(dt)`, `on_start()`, `on_destroy()` | Verified in test suites and native fixture | **PASS** |
| **Dependency Injection** | `@inject private input: Input` constructor injection | Verified with zero Service Locator calls | **PASS** |
| **ECS Requirements** | `@component private body: RigidBody2D` mandatory check | Verified rejection of entities missing component | **PASS** |
| **Optional ECS Handles** | `@optional private animator: Animator2D?` nullable check | Safe handling without crashing when component absent | **PASS** |
| **Exposed Properties** | `@expose({ min: 0.0, max: 1000.0 }) walkSpeed: Float` | Reflected in JSON manifest with min/max ranges | **PASS** |
| **Collections & Generics** | Deterministic `Array<T>`, `Map<K, V>`, `Result<T, E>` | Fully typed, monomorphized without template leakage | **PASS** |
| **Structured Async** | `Task<Void>`, `delay(500.ms)`, `TaskScope` cancellation | Verified cancellation on entity destruction | **PASS** |
| **Dynamic Hot Reload** | Snapshot properties -> destroy -> reload -> recreate -> restore | Fully preserved authored state across reload | **PASS** |
| **Hot Reload Failure** | Invalid new code stops script execution and reports the failure | Verified no fallback to stale gameplay code | **PASS** |
| **Scene Serialization** | Save/load behaviour state with stable type identities | Verified JSON roundtrip without C++ pointers | **PASS** |
| **VSCode / LSP Tooling** | Live diagnostics, document symbols, completion, hover | Verified JSON-RPC protocol implementation | **PASS** |

---

## 3. Validation Still Required

- Reproducible cold and incremental compilation timings.
- Hot reload latency and runtime allocation captures.
- Generated module and packaged extension sizes.
- Native builds and VSIX validation on macOS, Windows, and Linux.
- Mobile static-module/export validation before iOS or Android claims.

---

## 4. Exit Gate Decision

**Decision:** **PROCEED / CONTINUE**.
EngineScript is the selected gameplay scripting architecture. The local spike
validates typed authoring, DI metadata, compiled C++ syntax, lifetime-safe async
cancellation, and fail-stop reload. The remaining platform/export gates stay
visible in the active roadmap.

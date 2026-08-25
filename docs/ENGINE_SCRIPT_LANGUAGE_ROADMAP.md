# EngineScript Language and Compiler Roadmap

> **Status:** active implementation. EngineScript is the selected gameplay
> architecture. The compiler frontend, TypedIR, C++ emitter, Play Mode adapter,
> metadata, cache, LSP, and local platformer gates are implemented. Native
> export and cross-platform validation remain open.

`EngineScript` and the `.e2s` extension are the current product contract.

## 1. Product Intent

The proposed language is a statically typed, object-oriented gameplay language
that transpiles to C++20. It is not intended to become a general-purpose C++
replacement. Its purpose is to express Engine2D behaviours with minimal
boilerplate while making dependency injection, ECS component requirements,
Inspector properties, asynchronous work, serialization, and hot reload explicit
parts of the language contract.

The proposed authoring experience is external to Engine2D. Source files live in
the game project's `Source/` directory and are edited through VSCode. Engine2D
continues to own visual property authoring, dependency diagnostics, Play Mode,
Undo/Redo, and build status; it does not gain an embedded code editor.

Example syntax under evaluation:

```typescript
@behaviour
export class PlatformerPlayer extends Behaviour {
    constructor(
        @inject private input: Input,
        @component private body: RigidBody2D,
        @optional private animator: Animator2D?
    ) {}

    @expose({ min: 0, max: 1000 })
    walkSpeed: Float = 220.0;

    @expose
    jumpSpeed: Float = 480.0;

    override update(deltaTime: Float): Void {
        body.velocity.x = input.axis("Horizontal") * walkSpeed;
        if (input.pressed("Jump") && body.isGrounded) {
            body.velocity.y = -jumpSpeed;
        }
    }

    async respawn(): Task<Void> {
        await delay(500.ms);
        animator?.play("idle");
    }
}
```

## 2. Proposed Design Principles

- Static typing is mandatory; there is no implicit `any` or untyped table model.
- Behaviours are objects owned by Engine2D and attached to ECS entities.
- Composition is preferred over deep inheritance hierarchies.
- A class has at most one implementation base and may implement multiple
  interfaces.
- `struct` is a value type; `class` is a managed reference type; `behaviour` is
  engine-owned.
- Raw pointers, manual `delete`, arbitrary pointer arithmetic, and unchecked ECS
  references are not language features.
- Nullability is explicit through `T?`.
- Expected failures use `Result<T, Error>`; general exceptions are excluded from
  the initial language.
- Dependency injection is explicit and no global Service Locator is exposed.
- Collections used by gameplay have deterministic semantics by default.
- Async code resumes on the main thread unless it explicitly enters the job
  system.
- Generated C++ and build products live under `Library/` and are never authored
  or versioned.
- Editor serialization stores stable behaviour type identities and property
  values, never C++ pointers, offsets, or generated file paths.
- The generated module exposes a versioned ABI and must be rejected safely when
  incompatible.
- No legacy scripting compatibility layer or dual permanent scripting architecture is added.

## 3. Compiler Pipeline

```text
Source/*.e2s
    |
    v
SourceManager
    |
    v
Lexer -----------> tokens
    |
    v
Parser ----------> AST
    |
    v
SymbolResolver --> resolved symbols and scopes
    |
    v
TypeChecker -----> validated types and semantics
    |
    v
TypedIR
    |\
    | +----------> MetadataEmitter --> editor/DI manifest
    |
    +------------> CppEmitter ------> generated C++20
                                           |
                                           v
                                      Clang / MSVC
                                           |
                                           v
                                      GameModule

DependencyGraph controls invalidation and build order.
CompilerCache reuses valid intermediate and generated artifacts.
DiagnosticEngine receives diagnostics from every stage.
```

### 3.1 SourceManager

`SourceManager` owns source buffers and their identity. It converts internal
byte offsets into project-relative paths, lines, columns, snippets, and source
ranges. Tokens and syntax nodes retain `SourceSpan` values rather than raw file
pointers.

It is responsible for:

- stable `FileId` allocation;
- normalized project-relative source paths;
- line indexes and UTF-8-aware source locations;
- in-memory document versions supplied by VSCode before a file is saved;
- source snippets for diagnostics;
- mapping generated output and stack traces back to `.e2s` source;
- rejecting imports that escape the project source boundary.

It does not parse or interpret the language.

### 3.2 Lexer

`Lexer` converts characters into tokens. For example,
`speed: Float = 220.0;` becomes an identifier, colon, type identifier, equals,
floating-point literal, and semicolon.

It recognizes keywords, identifiers, numeric and string literals, decorators,
operators, punctuation, comments, and duration suffixes. It reports lexical
errors such as invalid characters, unterminated strings, malformed escapes, and
invalid numeric suffixes. It does not resolve whether a name or type exists.

### 3.3 Parser

`Parser` consumes tokens according to the language grammar and produces syntax
nodes. It recognizes modules, imports, declarations, decorators, object types,
functions, control flow, expressions, generics, and async constructs.

The parser must recover after malformed input so a partially edited VSCode
document can still produce useful structure and multiple diagnostics in one
pass. Expression precedence should use a Pratt parser or an equivalently explicit
precedence model.

### 3.4 AST

The Abstract Syntax Tree is the data model created by the parser. It preserves
the structure written by the user: original declarations, names, decorators,
expressions, declared types, and source spans.

An AST may still be semantically invalid. For example, the parser can represent
`let speed: Float = "fast";`; rejecting the incompatible initializer belongs to
the type checker.

### 3.5 SymbolResolver

`SymbolResolver` determines which declaration each name refers to. It builds
module, class, function, and local scopes; resolves imports and exports; enforces
visibility; connects member access; and identifies duplicate, unknown, private,
or ambiguous symbols.

Resolution establishes identity but does not by itself prove that all operations
are type-correct.

### 3.6 TypeChecker

`TypeChecker` validates assignments, calls, returns, operators, nullability,
inheritance, interface implementations, generic constraints, collection element
types, async/await use, ownership rules, and thread-boundary restrictions. It
also performs bounded local type inference.

It must report language-native errors. Users should not need to interpret C++
template diagnostics to understand an EngineScript type error.

### 3.7 TypedIR

The typed intermediate representation contains only resolved, validated
operations. It is intentionally simpler than the AST. Syntax sugar such as
optional chaining, high-level loops, duration literals, and generic calls is
lowered into explicit typed operations.

The AST represents what the author wrote. TypedIR represents what the program
must execute. Backends consume TypedIR and do not repeat name or type resolution.

### 3.8 CppEmitter

`CppEmitter` converts TypedIR into C++20 headers and sources. It generates
behaviour classes, functions, factories, generic instantiations, coroutine
implementations, type registration, and the GameModule entry point.

Generated sources include `#line` mappings or equivalent source-map metadata so
native compiler diagnostics point back to `.e2s` locations. The emitter is not
allowed to make semantic decisions; invalid TypedIR indicates a compiler bug.

### 3.9 MetadataEmitter

`MetadataEmitter` produces a deterministic, versioned manifest for the editor.
It records behaviour identities, properties, defaults, decorators, ranges,
assets, events, injected services, required/optional components, and supported
serialization operations.

The editor must be able to construct an Inspector and validate dependencies from
this manifest without loading unvalidated gameplay code.

### 3.10 DependencyGraph

`DependencyGraph` records import, inheritance, interface, public-signature,
implementation, generic-instantiation, decorator, metadata, and ABI edges. It
determines build order, detects forbidden cycles, and calculates the minimal
transitive invalidation set after a change.

A private method-body edit should not invalidate unrelated modules. A public
type-layout or signature change must invalidate every dependent artifact.

### 3.11 DiagnosticEngine

`DiagnosticEngine` is the single structured error channel used by every
compiler stage. A diagnostic has a stable code, severity, primary span,
secondary spans, explanatory notes, and optional suggested fixes.

The same diagnostic model is rendered for the CLI, Engine2D Console, automated
tests, and VSCode/LSP. Compiler stages do not print ad-hoc messages directly.

### 3.12 CompilerCache

`CompilerCache` stores valid tokens, ASTs, exported symbols, TypedIR, metadata,
generated C++, native object files, and concrete generic instantiations.

Cache keys include content hashes, compiler and language versions, SDK/ABI hash,
platform, compiler options, and public dependency hashes. Correct invalidation
takes priority over cache hit rate; file timestamps alone are insufficient.

## 4. Proposed Type and Ownership Model

### 4.1 Primitive and Engine Types

| EngineScript | Initial native representation |
| --- | --- |
| `Bool` | `bool` |
| `Int8`, `Int16`, `Int32`, `Int64` | fixed-width signed integer |
| `UInt8`, `UInt16`, `UInt32`, `UInt64` | fixed-width unsigned integer |
| `Int` | alias of `Int32` |
| `UInt` | alias of `UInt32` |
| `Float` / `Float32` | `float` |
| `Float64` | `double` |
| `String` | Engine2D string wrapper |
| `Vec2`, `Vec3`, `Vec4` | Engine2D/GLM value wrapper |
| `Color` | Engine2D color value |
| `Duration` | strongly typed duration |
| `Entity` | validated entity handle |
| `Asset<T>` | stable typed asset handle |
| `T?` | optional value/reference |
| `Result<T, E>` | explicit success/failure |

Potentially lossy numeric conversions use explicit type calls such as
`Int(speed)` or `Float64(score)`. Reference casts and `as`/`as?` conversion
syntax are intentionally excluded; `as` remains an import-alias keyword.

### 4.2 Objects

- `struct` has value semantics.
- `class` has reference semantics using an engine-owned reference-counted handle.
- `Weak<T>` breaks ownership cycles explicitly.
- `behaviour` instances are owned by their entity and cannot outlive their Play
  Session.
- ECS components are accessed through validated typed handles, not persisted raw
  references.
- Raw pointers and manual allocation are excluded from the initial language.

The exact class ownership model is an R0 decision and must be proven against
cycles, hot reload, and module unload before syntax is finalized.

### 4.3 Collections

The initial standard library should provide:

- `Array<T>`: dynamic ordered sequence;
- `FixedArray<T, N>`: fixed-size sequence;
- `Map<K, V>` and `Set<T>`: deterministic gameplay collections;
- explicit `HashMap<K, V>` and `HashSet<T>` variants if unordered performance is
  required;
- `Optional<T>`/`T?` and `Result<T, E>`.

`Map`/`Set` key types must satisfy appropriate equality, ordering, or hashing
constraints. Iteration semantics are part of the language contract rather than
an accidental consequence of an STL implementation.

### 4.4 Generics

Generics are type-checked in EngineScript and monomorphized into concrete C++
template instantiations. Implementation order is built-in generic containers,
generic functions, generic structs/classes, constraints, and bounded inference.

Unresolved templates do not cross the GameModule ABI. The compiler must monitor
native code growth and reuse identical valid instantiations through the cache.

## 5. Decorators, Dependency Injection, and Reflection

The initial compiler recognizes only versioned, built-in decorators:

```text
@behaviour
@serializable
@inject
@component
@optional
@expose
@range
@asset
@editorOnly
@deprecated
```

Arbitrary user compiler plugins or decorators that execute unrestricted code are
outside the initial scope.

Dependency scopes are application, Play Session, scene, entity, and behaviour.
Constructor injection is the preferred visible model. The compiler generates
factories and a dependency descriptor, detects service cycles and invalid scope
captures, and rejects required entity components that are absent at Play start.

Injection never silently mutates authored content. The editor may offer an
explicit reversible command to add an unambiguous missing component.

## 6. Async Execution Model

C++20 coroutines provide the generated state machines but not an execution
policy. Engine2D therefore owns `Task<T>`, coroutine promise types, awaitables,
and an `AsyncScheduler` integrated with the application loop.

Initial awaitables under evaluation:

```typescript
await nextFrame();
await fixedUpdate();
await delay(2.seconds);
await realDelay(500.ms);
await event<EnemyDefeated>();
await assets.load<Texture>(path);
await jobs.run(() => calculatePath());
```

Rules:

- tasks resume on the main thread unless they explicitly enter `jobs.run`;
- ECS component handles cannot cross into worker execution;
- worker values must satisfy a `Send` constraint;
- each behaviour owns a structured `TaskScope`;
- destroying an entity, stopping Play, unloading a scene, or hot reloading a
  module cancels the affected scope;
- coroutine frames are never migrated across hot reload;
- `update()` is synchronous so a new overlapping task is not created every frame;
- dropped or failed unobserved tasks produce structured diagnostics;
- deterministic ready-queue ordering is specified and tested;
- game-time and real-time delays are distinct;
- initial async failure propagation uses `Result`, not cross-module exceptions.

## 7. GameModule and Hot Reload Contract

Desktop development uses a new versioned dynamic module per successful build.
Exported/mobile builds link the generated module statically.

The proposed boundary is a C ABI entry point carrying an API version and SDK
hash. The engine rejects incompatible or incomplete modules before destroying
the last valid behaviour instances.

Hot reload sequence:

1. Compile into a new unique path.
2. Validate native build result, ABI, and metadata.
3. Snapshot compatible exposed properties.
4. Cancel owned async tasks.
5. Stop and destroy old behaviour instances.
6. Load the new module.
7. Recreate instances and inject dependencies.
8. Restore compatible property values.
9. Resume lifecycle.
10. Roll back to the last known-good module on any failure.

Loaded Windows DLLs are shadow-copied; no platform overwrites the currently
loaded binary. State migration never includes native pointers, module-owned
objects, or suspended coroutine frames.

## 8. Project and Editor Contract

Proposed project layout:

```text
GameProject/
  project.engine2d
  Assets/
    Scripts/
  ProjectSettings/
  Library/
    EngineScript/
      Cache/
      Generated/
      Build/
      HotReload/
```

Required editor capabilities include behaviour creation/attachment, manifest-
driven property controls, dependency display and fixes, behaviour ordering,
Undo/Redo, stable scene serialization, build diagnostics, fail-stop hot reload
status, and an Open in VSCode action.

EngineScript source is not edited inside Engine2D. Runtime-attached `.e2s` files
are Script assets under `Assets/`; generated C++ and caches remain in `Library/`
or build output. Scene data stores stable asset handles and exposed values rather
than generated C++ details.

## 9. VSCode and Language Server Contract

The compiler frontend must be reusable as a library so the language server uses
the exact same lexer, parser, resolver, type checker, and diagnostics as builds.

Planned tooling stages:

1. syntax highlighting, snippets, bracket matching, and build tasks;
2. live diagnostics and document symbols;
3. completion, hover, definitions, references, and signature help;
4. rename, formatting, workspace symbols, and code actions;
5. source-mapped stack navigation;
6. a debug adapter only after the compiler/runtime contracts are stable.

The current extension covers diagnostics, symbols, completion, hover, and build
commands. References, rename, formatting, code actions, and source-mapped runtime
navigation remain planned.

## 10. Implementation Roadmap

Every milestone has an explicit exit gate. Passing compilation alone is never
sufficient.

### R0 - Language Constitution

- Decide final name, extension, goals, and non-goals.
- Decide object ownership, nullability, numeric conversion, error propagation,
  inheritance, deterministic collection, and compatibility policies.
- Define language and SDK versioning.
- Produce accepted valid/invalid example programs.

**Exit gate:** each basic example has an unambiguous documented meaning.

### R1 - Grammar and Semantic Examples

- Write the EBNF grammar, keywords, operator precedence, declarations, modules,
  object syntax, decorators, generics, async syntax, and collection literals.
- Write platformer, inventory, state machine, damage, service injection, event,
  async, asset, and UI examples plus their expected diagnostics.

**Exit gate:** the complete MVP surface is representable without placeholder
syntax.

### R2 - Source and Diagnostic Foundation

- Implement `FileId`, `SourceLocation`, `SourceSpan`, line indexing, virtual
  documents, diagnostic codes, notes, fixes, CLI rendering, and JSON/LSP output.
- Test UTF-8, LF/CRLF, empty/large files, multiline spans, virtual edits, and
  multiple diagnostics.

**Exit gate:** every source range is reported consistently in CLI and test output.

### R3 - Lexer

- Implement all tokens, trivia, literals, comments, decorators, duration suffixes,
  lexical recovery, token snapshots, and fuzz coverage.

**Exit gate:** arbitrary input returns tokens/diagnostics without crashes or
infinite loops.

### R4 - Parser and AST

- Implement AST nodes, declarations, Pratt expression parsing, object and generic
  syntax, async constructs, imports, recovery/synchronization, and AST dumps.
- Test incomplete editor buffers and multiple errors per file.

**Exit gate:** all R1 examples produce stable ASTs or expected parse diagnostics.

### R5 - Modules, Symbols, and Dependency Graph

- Implement scopes, imports/exports, visibility, shadowing, overload sets, module
  cycles, stable symbol IDs, public API hashing, dependency edge classification,
  and deterministic build order.

**Exit gate:** a multifile project resolves consistently and a private edit does
not invalidate unrelated modules.

### R6 - Core Type Checker

- Implement primitives, variables, functions, returns, operators, conversions,
  nullability, values/references, classes, structs, interfaces, behaviours,
  control-flow checking, definite assignment, unreachable code, and local
  inference.

**Exit gate:** valid synchronous nongeneric programs are fully typed and invalid
programs never depend on C++ diagnostics.

### R7 - TypedIR

- Define TypedIR, AST lowering, optional-chain and loop desugaring, resolved calls,
  control-flow representation, stable internal IDs, and an invariant verifier.

**Exit gate:** backends can operate without consulting source scopes or AST types.

### R8 - Minimal C++20 Backend

- Generate headers, sources, stable internal names, values, objects, functions,
  behaviours, factories, type registration, and source mappings.
- Compile and execute generated fixtures with Clang; define the MSVC contract.

**Exit gate:** synchronous EngineScript fixtures produce and execute valid native
code with errors mapped to original sources.

### R9 - Metadata and Reflection

- Emit deterministic versioned manifests for types, stable identities, properties,
  defaults, decorators, events, dependencies, and serialization.
- Test rename/removal/type-change/default-change behavior.

**Exit gate:** a headless Inspector model can be created from metadata alone.

### R10 - Dependency Injection

- Implement scopes, constructor injection, component requirements, optional
  dependencies, cycle/scope validation, generated factories, reinjection, test
  fakes, editor diagnostics, and reversible missing-component fixes.

**Exit gate:** the synchronous platformer behaviour uses no Service Locator or
duplicated manual registration.

### R11 - Collections and Generics

- Implement deterministic arrays/maps/sets, optional/result, generic functions,
  generic structs/classes, constraints, bounded inference, monomorphization,
  instantiation reuse, and native code-growth reporting.

**Exit gate:** generic examples compile without leaking C++ template diagnostics.

### R12 - Compiler Cache

- Cache source-independent stages and native artifacts using content/version/SDK
  hashes; implement transitive invalidation, cache statistics, corruption recovery,
  and cleanup.

**Exit gate:** a local method edit rebuilds only the minimal verified dependency
closure.

### R13 - Async and Structured Concurrency

- Implement `Task<T>`, C++20 coroutine emission, scheduler queues, game/real time,
  events, assets, explicit worker jobs, `Send`, cancellation, task scopes, and
  unobserved failure diagnostics.

**Exit gate:** no task survives its entity/scene/session accidentally and all
scheduler tests are deterministic.

### R14 - GameModule and Hot Reload

- Implement versioned ABI, SDK hash, dynamic/static module variants, shadow copy,
  property snapshots, instance recreation, reinjection, fail-stop errors, and
  clean teardown.

**Exit gate:** a failed build or incompatible module stops gameplay explicitly,
preserves serialized authored properties, and reports the failure without
silently running stale code.

### R15 - Engine2D Editor Integration

- Add the project `Source/` boundary, build orchestration, diagnostics, behaviour
  authoring, manifest-driven Inspector, ordering, Undo/Redo, dependency fixes,
  scene serialization, status UI, and VSCode opening.

**Exit gate:** authoring, save/reopen, Play, Stop, and hot reload preserve exact
scene state without manual JSON editing.

### R16 - VSCode and LSP

- Deliver highlighting/build tasks, then compiler-backed diagnostics, symbols,
  completion, hover, definition, references, rename, formatting, code actions,
  and source navigation.

**Exit gate:** normal code authoring does not require reading generated C++.

### R17 - Mandatory Platformer Spike

The spike must cover behaviour lifecycle, Input DI, required Rigidbody2D,
optional Animator2D, exposed values, Array/Map/Set, one generic helper, one event,
one async operation, cancellation, asset identity, Undo/Redo, scene reload, hot
reload, intentional compiler failure, dependency failure, and fail-stop behavior.

Measure cold/incremental build, reload latency, module size, compiler/runtime
memory, generated code, diagnostic quality, and macOS/Windows/iOS build strategy.

**Exit gate:** an explicit continue/reduce/change-language/abandon decision.

### R18 - EngineScript Adoption and Legacy Removal

Only after R17 approval:

- migrate authored gameplay, runtime services, serialization, Inspector, tests,
  project templates, documentation, and external project source;
- remove legacy scripting bindings, runtime, analyzer, CMake targets, templates,
  tests, and unused vendored sources;
- run all engine tests plus standalone and supported-platform validation;
- update the active roadmap and scripting contract.

There is no backward-compatibility layer because Engine2D has no released script
format. The migration is a clean replacement and must not leave two competing
runtime architectures.

## 11. Planned Test Targets

The names are provisional but define required coverage boundaries:

- `EngineScriptSourceTests`
- `EngineScriptLexerTests`
- `EngineScriptParserTests`
- `EngineScriptResolverTests`
- `EngineScriptTypeCheckerTests`
- `EngineScriptIRTests`
- `EngineScriptCodegenTests`
- `EngineScriptMetadataTests`
- `EngineScriptDependencyTests`
- `EngineScriptCacheTests`
- `EngineScriptDITests`
- `EngineScriptGenericTests`
- `EngineScriptAsyncTests`
- `EngineScriptModuleTests`
- `EngineScriptHotReloadTests`
- `EngineScriptEditorIntegrationTests`
- `EngineScriptProjectTests`
- `EngineScriptLspTests`

Every regression is added at the lowest responsible layer. Generated C++ must be
compiled in tests, not validated only through text snapshots. Hardware and real
platform checks remain opt-in and separately labeled.

## 12. Decision Gates and Non-Goals

The project could have stopped after R1, R8, or R17. Those historical gates
existed to avoid continuing only because compiler work had already begun.

Initial non-goals:

- a general-purpose systems language;
- arbitrary native pointers or manual memory management;
- user-defined compiler plugins/macros;
- a package registry;
- an embedded script editor;
- JIT compilation in exported games;
- dynamic code loading on mobile exports;
- coroutine frame migration across hot reload;
- untrusted mod sandboxing;
- compatibility with unreleased legacy script content after final migration.

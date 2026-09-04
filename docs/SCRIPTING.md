# EngineScript Scripting Contract

EngineScript is Engine2D's only gameplay scripting architecture. It is a
statically typed, object-oriented language stored in `.e2s` assets. The shared
compiler frontend produces diagnostics, editor metadata, TypedIR, and C++20.
Luau is not an active runtime or supported authoring format.

## Authoring boundary

Game code belongs to the external project, normally under `Assets/Scripts/`.
Engine2D intentionally has no embedded code editor. Open the game project in
VSCode and install the extension in `editors/vscode`; it provides syntax
highlighting, compiler-backed diagnostics, completion, hover, symbols, and the
`EngineScript: Compile Project` command.

The extension selects `e2sc` and `e2s-lsp` for the current operating system and
architecture. Custom installations may set `enginescript.compilerPath` and
`enginescript.serverPath`.

## Behaviour assets

A runtime script asset exports one `@behaviour` class derived from `Behaviour`:

```enginescript
@behaviour
export class PlatformerPlayer extends Behaviour {
    constructor(
        @inject private input: Input,
        @component private body: RigidBody2D,
        @optional private animator: Animator2D?
    ) {}

    @expose({ min: 0.0, max: 1000.0 })
    walkSpeed: Float = 280.0;

    @expose
    jumpSpeed: Float = 520.0;

    override update(deltaTime: Float): Void {
        let direction: Float = input.axis("Horizontal");
        body.velocity.x = direction * walkSpeed;
        body.setIgnoreOneWay(input.axis("Vertical") < -0.5);

        if (input.pressed("Jump") && body.isGrounded) {
            body.velocity.y = jumpSpeed;
        }

        animator?.play("Run");
    }
}
```

Constructor decorators define dependencies rather than hiding service lookups:

- `@inject` requests an engine service such as `Input`.
- `@component` requires a component on the owning entity. Instantiation fails
  with a diagnostic when it is missing.
- `@optional` supplies a nullable component binding.
- `@expose` publishes a typed property to metadata and the Inspector.

## Serialized state and live fields

`ScriptComponent` serializes the script asset handle, enabled state, and typed
public values. Supported Inspector values currently include numbers, booleans,
strings, enums, `Vec2`, entity references, and asset handles. Enums are stored by
member name and rendered as dropdowns. If a serialized member was removed from
the enum, the Inspector preserves it and marks it invalid instead of silently
choosing another value. A string-backed enum may use its backing text when
interpolated, but persistence still uses the declaration member name. The
compiler manifest is the schema source; matching authored values survive
recompilation and missing values receive their declared defaults.

Inspector edits during Play Mode are read by the behaviour on the following
update. Script-side changes affect only the runtime component. Stop always
restores the authoring snapshot and discards runtime changes.

## Play Mode execution

At Play start the runtime compiles all ready `.e2s` assets as one project, so
imports resolve across files. A project compilation error prevents behaviour
startup and places affected `ScriptComponent` instances in an error state.

The editor runtime creates `GameModule` behaviour registrations backed by the
compiler's TypedIR and executes the authored lifecycle bodies. It does not
contain a platformer controller or other game-specific fallback. The currently
connected execution surface includes:

- `on_start`, `update`, `on_destroy`, and `on_collision_enter`;
- local declarations, assignments, arithmetic, comparisons, boolean operators,
  and `if`/`else`;
- live exposed-property reads and writes;
- named, remappable Input axes/actions: `axis`, `down`, `pressed`, and `released`;
- required `RigidBody2D` velocity and grounded access, including temporary
  one-way-platform passthrough with `setIgnoreOneWay`, plus typed Transform,
  SpriteRenderer, Time, Display, physics hit, asset, particle, and joint properties;
- optional `Animator2D.play` and runtime entity hierarchy operations through `Entities`.

`GameplayTagsComponent` provides stable semantic identity independently of the
author-facing entity name. Tags use lowercase dotted names such as `player`,
`camera.main`, and `collectible.coin`. They are serialized in scenes and prefabs,
editable in the Inspector, and available to scripts through `Entities`:

```enginescript
let player: Entity = entities.findByGameplayTag("player");
let optionalPlayer: Entity? = entities.firstByGameplayTag("player");
let coins: Array<Entity> = entities.findAllByGameplayTag("collectible.coin");
if (entities.hasGameplayTag(other, "platform.trampoline")) {
    body.velocity.y = trampolineSpeed;
}
```

Runtime-created entities can use `addGameplayTag` and `removeGameplayTag`;
`gameplayTags(entity)` returns the current sorted list. New code should use
`firstByGameplayTag` when absence is expected and bind its `Entity?` explicitly.
The compatible `findByGameplayTag` method still returns an invalid `Entity` for
which `entities.exists(...)` is `false`; invalid or destroyed
references otherwise return `false` or an empty array rather than exposing stale
ECS storage. Display-name queries remain available for authoring-oriented lookups,
but gameplay behavior should prefer semantic tags.

`@inject` can also request the following gameplay services:

- `Audio`: `playSound`, `playMusic`, music transport, and master/music/SFX
  volume controls;
- `Scene`: `load(path)`, `currentName()`, `isLoading()`, and `lastError()`.
  A successful request advances through two frame boundaries before the new
  runtime scene is activated. Gameplay events use authored struct values; the
  string event bus is not part of the current EngineScript contract.
- `Assets`: `find(path)`, `exists(path)`, and the typed async operation
  `await assets.load<Texture|AudioClip|Font|DataAsset|ParticleEffect2D>(path)`.
  Loading validates the registered asset type and precaches it on the next
  frame. It returns `Result<Asset<T>, String>` so unavailable paths and type
  mismatches are explicit values rather than `null` or string events.

These services use the same calls in editor Play Mode and generated native
modules. Audio and scene requests are routed through the host runtime, so a
headless host may deliberately leave a service unavailable.

`@inject private particles: Particles` emits authored `.particle` assets without
exposing renderer objects. Load the effect through `Assets`, then emit it at a
world position:

```enginescript
async spawnDust(position: Vec2): Task<Void> {
    let loaded: Result<Asset<ParticleEffect2D>, String> = await assets.load<ParticleEffect2D>(
        "Assets/Particles/dust.particle"
    );
    if (loaded.isOk()) particles.emit(loaded.value(), position, 12);
}
```

An entity with `ParticleEmitter2DComponent` can bind
`@component private emitter: ParticleEmitter2D`. `play()`, `stop(clear)`, and
`burst(count)` control its scene-owned emitter; `setEffect(asset)` accepts only
a ready `ParticleEffect2D`. `active`, `emitRate`, and `burstCount` are writable
runtime values, while `isPlaying` and `effectHandle` are read-only. For bounded
one-shot customization use `ParticleEmitOptions` with
`particles.emitWithOptions(effect, position, options)`. Per-frame, per-emitter,
and per-scene limits reject excess emissions instead of overwriting live
particles.

Create a Particle Effect from the Asset Browser and open it to edit colors,
texture, motion, lifetime, collision mask, emission defaults, render layer,
depth, and Alpha/Additive blending. Its isolated preview supports Play/Pause,
Restart, Burst, and Clear. Asset edits and emitter Inspector changes participate
in editor Undo/Redo; runtime buttons are intentionally available only in Play
Mode. Stop and scene detach clear the scene particle pool.

`@inject private prefs: PlayerPrefs` exposes typed preferences only: Bool,
numbers, String, Vec2, and Vec3 values can be read with fallbacks, written,
removed, cleared, and saved. It never exposes project paths or arbitrary file
access. Use the canonical camelCase SDK names; unsupported legacy aliases are
compiler errors rather than calls that can fail later during native compilation.

EngineScript removes the legacy `toString(value)` helper. Use the `String`
type as an explicit numeric conversion instead: `String(score)`. Numeric values
are never converted implicitly, so `"Score: " + score` is rejected while
`"Score: " + String(score)` is valid and remains statically typed.

Numeric types use their type name as an explicit conversion: `Int(speed)`
truncates a floating value toward zero and saturates to the `Int32` range, while
`Float(score)` converts an integer using IEEE-754 rounding. The same syntax is
available for `Int8` through `Int64`, `UInt8` through `UInt64`, `Float32`, and
`Float64`. Integer narrowing and signedness changes saturate; NaN converts to
zero for integer destinations, and infinities convert to the corresponding
limit. `Int`, `UInt`, and `Float` remain aliases of `Int32`, `UInt32`, and
`Float32`. `Vec2(...)`, `Vec3(...)`, `Vec4(...)`, and `Color(...)` are value
constructors, not casts. Components remain typed ECS queries through
`entity.getComponent<T>()`.

## Ergonomic control flow and text

Backtick strings support multiple lines and `${expression}` interpolation. Each
expression is evaluated exactly once from left to right. Conversion is
deterministic and locale-independent for strings, numbers, booleans, durations,
enums, and documented math values. Integer padding and floating precision are
checked statically:

```enginescript
let label = `Wave ${wave:02} · Damage ${damage:.2f}`;
```

An authored type is interpolable when it has a public synchronous
`toString(): String` method; no marker interface is required. Use `\${` for a
literal interpolation opener and `` \` `` for a literal backtick. Quoted strings
and `String(number)` remain valid. Implicit concatenation such as
`"Score: " + score` remains an error.

Nullable values can be bound without repeating a lookup:

```enginescript
if (let target = entities.firstByGameplayTag("enemy.boss")) {
    target.destroy();
}

guard (let player = entities.firstByGameplayTag("player")) else { return; }
let stateLabel = alive ? "Alive" : "Defeated";
```

The nullable expression is evaluated once. An `if let` binding exists only in
its positive block. A `guard let` binding exists after the guard, and its `else`
block must end with `return`, `break`, or `continue`.

Enums provide stable gameplay states and exhaustive `match` expressions or
statements without fall-through:

```enginescript
enum EnemyState { Idle, Chase, Attack }

let speed = match (state) {
    EnemyState.Idle => 0.0,
    EnemyState.Chase => walkSpeed,
    EnemyState.Attack => attackSpeed
};
```

Enums, booleans, and nullable domains must be exhaustive. Numeric and string
domains require a final `_` arm. Prefer enums over magic state integers; do not
add a wildcard to a closed enum merely to hide an unhandled new member.

Functions, methods, and constructors accept constant trailing defaults and named
arguments after positional arguments. Named calls are normalized to declaration
order before IR generation:

```enginescript
function damage(base: Int, multiplier: Int = 1, critical: Bool = false): Int {
    return critical ? base * multiplier * 2 : base * multiplier;
}
let result = damage(20, critical: true);
```

Defaults may be literals, `null`, enum members, or constant native value
constructors. Required parameters cannot follow a default parameter. Unknown,
duplicated, missing, or positional-after-named arguments are compile errors.

`Deque<T>` is the deterministic queue type. It exposes `length`, `size`,
`isEmpty`, `pushFront`, `pushBack`, `front`, `back`, `popFront`, `popBack`, and
`clear`. Reads and removals from an empty deque return `T?`:

```enginescript
let pending: Deque<SpawnRequest> = Deque<SpawnRequest>();
pending.pushBack(request);
if (let next = pending.popFront()) spawn(next);
```

When related values move together, use one collection of an authored struct.
Parallel arrays or deques can diverge after an insertion/removal and are an
authoring anti-pattern.

Generic functions, structs, and classes accept explicit concrete type arguments
and constraints such as `T extends Send`. Identical combinations are reused
across the workspace. Compilation is capped at 256 unique concrete generic
instantiations and 32 active expansion levels; `E3091` reports excessive code
growth before invoking C++, and `e2sc` prints the resulting instantiation count
and generated C++ byte size.

The external platformer demo keeps gameplay in project-owned EngineScript
behaviours: player movement, coyote time, jump buffering, double jump,
drop-through platforms, trampoline bounce, coin destruction, persistent high
score, HUD, music, and moving platforms. `SceneRuntime` advances reusable engine
systems and contains no tag-specific platformer fallback.

The C++ backend emits native `NativeBehaviour` subclasses and factories. Play
Mode compiles every valid script project to a versioned dynamic module, validates
its ABI, and creates native behaviour instances with ordered host bindings.
There is no interpreter fallback in the active runtime path.

## API contract and coverage

`src/enginescript/api/engine_api.json` is the current source of truth for
the gameplay SDK surface. The compiler resolves its builtin types and functions
from this catalog, and its ABI version and SDK hash are checked against the
native `GameModule` contract. Entries marked `planned` are intentionally not
visible to authored scripts yet.

Each method declares typed parameters, defaults, return nullability, and
availability. Each property declares read/write access and nullability. The
compiler and LSP consume those descriptors, and tests verify that runtime
services and component descriptors remain represented by the catalog.
The human-readable [API reference](ENGINESCRIPT_API.md) is regenerated with
`python3 scripts/generate_enginescript_api_docs.py`; `--check` detects drift.

Exposed properties use a typed bridge shared by the runtime and generated
behaviours: `Bool`, numbers, `String`, enums, `Vec2`, and `Entity` are pushed from the
Inspector before each lifecycle callback and pulled back afterwards. Asset
queries resolve the project's stable asset records without exposing filesystem
access to gameplay code.

The catalog excludes editor-only controls, SDL/GPU backend details, arbitrary
filesystem access, and engine-private state. Every new runtime system or
component must either add a documented, tested EngineScript binding with editor
Play/export parity or be explicitly recorded as non-scriptable.

Generated native modules expose `engine_script_module_exports` with C linkage.
The loader copies only POD descriptor data, validates the ABI version and SDK
hash, and then owns the dynamic-library lifetime. A native behaviour is always
destroyed before its module is unloaded.

`e2sc` writes `GameModule.generated.cpp` beside the generated behaviour files.
It registers every behaviour factory through that export symbol; native build
targets compile this file together with the generated behaviour sources.

`NativeModuleBuilder` has two explicit outputs: `DynamicLibrary` is used by
desktop Play Mode and the current packaged runtime, while `StaticSources`
writes the same generated sources and entrypoint for future static-link export
targets. Neither mode writes authored project content.

The runtime staging target invokes `e2sc --native-module` after copying the
project into the package. The standalone detects that staged module at
`Library/EngineScript/Generated/1` and loads it directly; it only uses the
development-time compiler fallback when that packaged artifact is absent.

## Hot reload

Editor Play Mode watches the source signature of active script assets. A change
compiles and ABI-loads a candidate module before replacing the active one. A
candidate failure keeps the active native behaviours running and reports the
reload error. A successful candidate recreates instances from the current
serialized component values.

## Async ownership

The native backend maps `Task<T>` and `await` to C++20 coroutines scheduled by
`AsyncScheduler`. Each scheduled suspension holds a checked lifetime token. If
its `Task` is discarded or its entity scope is cancelled, the scheduler removes
the entry without touching the destroyed coroutine frame. Authored async method
calls returning `Task<T>` are retained by their owning behaviour, advanced at
the frame boundary, and cancelled when that behaviour is destroyed or reloaded.
The native SDK supplies `Result<T, String>` with `success`, `failure`, `isOk`,
`isError`, `value`, and `error`. Construct results with explicit types:
`Result.success<Float, String>(value)` or
`Result.failure<Float, String>(message)`. This keeps error and success types
fully checked before native code generation.

## Expression support

The native backend supports literals, arithmetic and boolean expressions,
explicit numeric conversions, value and object construction, authored struct
object literals, property access and assignment, calls, `this`, arrow
functions, `await`, typed `Result` operations, deterministic `Array`, `Map`,
`Set`, and `FixedArray` literals, checked indexing, and indexed assignment.
Collection methods are typed against their element/key/value contracts and
lower to the deterministic runtime helpers. Reference casts are not part of the
current language; `as` is reserved for import aliases.

For tooling and AI audits, `e2sc <project> --check --json` returns one JSON object
with success and code-growth totals plus every diagnostic's code, severity,
file, range, explanation, and suggested fix. The API catalog exposes the current
language version, ABI, SDK hash, compiler-cache identifier, and feature flags.

## Standard library beta

`Math`, `Vec2`, `Vec3`, `Vec4`, `Color`, `Ease`, `Rect` and conservative
collection helpers are available through the current API catalog. Vector and
easing operations are pure; `Rect.intersection` returns a nullable `Rect`.
`Color` provides component operations only: EngineScript does not define an
sRGB/linear conversion contract yet.

`Random` is deliberately stateless and accepts an explicit `UInt` seed. Each
game owns its own lifecycle policy, typically through one game-side behaviour
attached to a dedicated entity:

```enginescript
@behaviour
export class GameRng extends Behaviour {
    @expose
    seed: UInt = UInt(42);

    nextFloat(min: Float, max: Float): Float {
        seed = Random.nextUInt(seed);
        return Random.rangeFloat(seed, min, max);
    }
}
```

The engine does not expose a shared RNG service; a game decides how other
behaviours reach its `GameRng` entity. `Array.shuffle(seed)` and
`Array.choose(seed)` are deterministic for the supplied seed; `choose` is
nullable for an empty array. Lazy iterators and renderer-wide linear output
remain post-beta work.

## Advanced utilities

The current renderer consumes sRGB `SDL_Color` values. `Color.srgbToLinear`
and `Color.linearToSrgb` are explicit conversion helpers for calculations;
they do not change the renderer's color-space contract. `Bounds2` supplies
center/size spatial bounds and `Noise` supplies seeded value/fBm noise.

`Ease` includes Bounce and Elastic curves, plus explicitly named unclamped
variants. `Array.map`, `filter`, `reduce`, and `sortBy` execute callbacks
eagerly: callbacks never escape or become persistent runtime state.

## Compiler cache and generated content

`EngineScriptWorkspace` hashes the project inputs and SDK contract. An unchanged
workspace reuses its last verified compilation; unchanged modules reuse emitted
C++ artifacts, `force_rebuild` bypasses reuse, and the dependency graph is
rebuilt from current imports on every real compilation. Generated files and
caches belong under `Library/` or build output, never under authored assets.

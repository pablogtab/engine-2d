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
        let direction: Float = input.axis("MoveHorizontal");
        body.velocity.x = direction * walkSpeed;

        if (input.pressed("Jump") && body.isGrounded) {
            body.velocity.y = -jumpSpeed;
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
strings, `Vec2`, entity references, and asset handles. The compiler manifest is
the schema source; matching authored values survive recompilation and missing
values receive their declared defaults.

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
- named Input axes/actions;
- required `RigidBody2D` velocity and grounded access;
- optional `Animator2D.play`.

`@inject` can also request the following gameplay services:

- `Audio`: `playSound`, `playMusic`, music transport, and master/music/SFX
  volume controls;
- `Scene`: `load(path)` and `currentName()`.

These services use the same calls in editor Play Mode and generated native
modules. Audio and scene requests are routed through the host runtime, so a
headless host may deliberately leave a service unavailable.

The C++ backend emits native `NativeBehaviour` subclasses and factories. Play
Mode compiles every valid script project to a versioned dynamic module, validates
its ABI, and creates native behaviour instances with ordered host bindings.
There is no interpreter fallback in the active runtime path.

## API contract and coverage

`src/enginescript/api/engine_api.json` is the versioned source of truth for
the gameplay SDK surface. The compiler resolves its builtin types and functions
from this catalog, and its ABI version and SDK hash are checked against the
native `GameModule` contract. Entries marked `planned` are intentionally not
visible to authored scripts yet.

Exposed properties use a typed bridge shared by the runtime and generated
behaviours: `Bool`, numbers, `String`, `Vec2`, and `Entity` are pushed from the
Inspector before each lifecycle callback and pulled back afterwards. Asset
references remain metadata-only until the asset runtime binding is available.

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
desktop Play Mode, while `StaticSources` writes the same generated sources and
entrypoint for an export target to compile and link statically. Neither mode
writes authored project content.

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
execution through the editor's TypedIR adapter is still pending.

## Compiler cache and generated content

`EngineScriptWorkspace` hashes the project inputs and SDK contract. An unchanged
workspace reuses its last verified compilation; unchanged modules reuse emitted
C++ artifacts, `force_rebuild` bypasses reuse, and the dependency graph is
rebuilt from current imports on every real compilation. Generated files and
caches belong under `Library/` or build output, never under authored assets.

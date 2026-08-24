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

The C++ backend emits native `NativeBehaviour` subclasses and factories. Tests
compile generated fixtures with the configured Clang, GCC, or MSVC toolchain;
native standalone module loading remains an explicit roadmap item.

## Hot reload

Editor Play Mode watches the source signature of active script assets. A change
recompiles the complete project and recreates behaviour instances while keeping
compatible serialized public values.

Reload is fail-stop. If the new project does not compile, a dependency cannot be
resolved, or an instance cannot be created, all script behaviours stop and the
Inspector reports the error. Engine2D never hides the failure by returning to
the previous implementation. Saving a later valid version starts fresh
instances from the current serialized component values.

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

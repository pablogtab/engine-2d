# EngineScript 2.0 Migration

EngineScript 2.0 is a clean language and runtime boundary. Generated 1.x
modules are invalidated by language version 2, ABI/SDK 26, and compiler cache
identity `enginescript_codegen_v2_string_cast`; there is no compatibility adapter.

## Removed APIs

- `toString(value)` was removed from the SDK, runtime, generated C++, and LSP.
  Replace it with the explicit type conversion `String(value)`. Concatenating
  a `String` and a number remains a type error; write `"Score: " +
  String(score)` instead.
- String-named gameplay events were replaced by authored struct events.
- `Assets.request(path)` and its string completion events were replaced by the
  typed fallible `assets.load<T>(path)` operation.
- Reference casts are not supported. `as` is only valid in import aliases;
  components are queried with `entity.getComponent<T>()`.

Internal C++ diagnostics and metadata may still use `std::to_string`, type
representation helpers, or equivalent engine-only formatting. The VS Code
extension may still call Node's `Buffer.toString()` when decoding process
output; neither is an EngineScript API.

## Direct replacements

```enginescript
let scoreFloat: Float = Float(score);
let scoreInt: Int = Int(speed);
let scoreLabel: String = "Score: " + String(score);

let loaded: Result<Asset<Texture>, String> =
    await assets.load<Texture>("Assets/player.png");

let event: EnemyDefeated = { enemy: entity, score: 10 };
events.emit(event);
let nextEvent: EnemyDefeated = await events.next<EnemyDefeated>();

let enemy: Enemy? = entity.getComponent<Enemy>();
```

Numeric conversions saturate deterministically. Float-to-integer conversions
truncate toward zero; NaN becomes zero and infinities become the destination
limit. Vector and color calls remain value construction rather than casts.

Worker jobs accept only inline closures whose captures are immutable `Send`
values copied into the worker. ECS handles, behaviours, `this`, mutable locals,
and unsafe references are rejected. Completion resumes through the main
scheduler, and behaviour-owned work is cancelled on entity destruction, module
unload, or script reload.

## Tooling

Install the EngineScript VS Code extension version 2.0.0. After replacing an
existing server binary, run `EngineScript: Restart Language Server`. The
extension never restarts VS Code automatically.

# Luau Scripting Contract

Engine2D embeds Luau 0.735. Runtime builds link only the compiler and VM. The
editor additionally links `Luau.Analysis` to report strict syntax and type
errors without adding the analyzer to exported games.

## Script assets

- Scripts use the `.luau` extension and must start with `--!strict`.
- A script module returns one table.
- `fields` is an optional ordered array of public field definitions.
- Lifecycle callbacks are optional: `on_create(context)`,
  `on_update(context, delta_time)`, and `on_destroy(context)`.
- Top-level module code must remain declarative. Engine bindings are available
  through the callback context, not while the module table is being created.

The Asset Browser's `New Script` action creates the canonical typed template.

## Public fields

Each field definition has a unique `name`, a supported `type`, and a matching
`default` value:

| Type | Luau default | Inspector control |
| --- | --- | --- |
| `boolean` | `true` or `false` | Checkbox |
| `number` | Number | Numeric field |
| `string` | String | Text field |
| `vector2` | `{x, y}` | Two-axis field |
| `entity` | Non-negative entity ID | Entity picker |
| `asset` | Asset handle string or `nil` | Asset picker |

The saved `ScriptComponent` owns only the selected script handle, enabled
state, and serialized public values. When a script schema changes, the editor
preserves fields whose name and type still match, adds new defaults, and drops
removed or type-changed fields through an Undo/Redo transaction.

## Runtime context

The initial context contract is:

```luau
type ScriptContext = {
    entity: number,
    fields: Fields,
    get_position: () -> (number, number, number),
    set_position: (number, number, number?) -> (),
    get_axis: (string) -> number,
    is_action_pressed: (string) -> boolean,
    is_action_just_pressed: (string) -> boolean,
    is_action_just_released: (string) -> boolean,
    get_velocity: () -> (number, number),
    set_velocity: (number, number) -> (),
    is_grounded: () -> boolean,
    set_sprite_flip_x: (boolean) -> (),
    play_animation: (string) -> boolean,
}
```

Input calls use the engine's named action and axis mappings, so scripts do not
depend on SDL scancodes. Physics, sprite, and animation calls operate only on
the entity that owns the script and report a runtime error when the required
component is absent. `play_animation` returns `false` when the requested clip
does not exist.

`assets/scripts/PlatformerPlayer.luau` is the first authored gameplay example.
It drives the saved platformer player's horizontal velocity, jump, facing, and
Idle/Run/Jump/Fall clips. The scene owns its speed and input names as editable
public fields. The temporary trampoline behavior and generated jump sound remain
in `GenericScene` until collision events and audio are available to scripts.

`fields` is copied into the runtime instance. Inspector changes to public fields
during Play are synchronized into that table on the next update, allowing live
tuning; Stop still restores the authoring snapshot. Script-side mutations never
write back to the serialized authoring fields. Each entity receives an isolated
module and context while compiled bytecode and field metadata are reused for
entities that share the same script asset.

## Play Mode isolation

Starting Play Mode captures the authoring snapshot before scripts run. Script
instances are created after the scene switches to runtime mode. Stopping calls
`on_destroy`, tears down the VM deterministically, and restores the snapshot,
including script components and public values.

Current scope intentionally excludes hot reload, Play Mode apply-back, audio,
collision events, arbitrary asset APIs, display APIs, and scene loading. These
remain Phase 7 work.

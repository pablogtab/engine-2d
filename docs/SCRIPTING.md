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

The runtime context contract is:

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
    play_sound: (asset: string, volume: number?, pitch: number?) -> (boolean, string?),
    play_music: (asset: string, volume: number?, loop: boolean?) -> (boolean, string?),
    stop_music: () -> (),
    pause_music: () -> (),
    resume_music: () -> (),
    set_volume: (channel: "master" | "music" | "sfx", volume: number) -> (),
    get_volume: (channel: "master" | "music" | "sfx") -> number,
    find_asset: (project_relative_path: string) -> string?,
    asset_exists: (asset: string) -> boolean,
    asset_path: (asset: string) -> string?,
    asset_type: (asset: string) -> string?,
    get_display_info: () -> {
        width: number,
        height: number,
        dpi_scale: number,
        reference_width: number,
        reference_height: number,
        window_mode: string,
        vsync: boolean,
        safe_area: { left: number, top: number, right: number, bottom: number },
    },
    set_window_mode: ("windowed" | "borderless_fullscreen" | "exclusive_fullscreen") -> (boolean, string?),
    set_vsync: (boolean) -> (boolean, string?),
    load_scene: (scene_asset: string) -> (boolean, string?),
    get_scene_name: () -> string,
    emit_event: (name: string, payload: string?) -> (),
}
```

Input calls use the engine's named action and axis mappings, so scripts do not
depend on SDL scancodes. Physics, sprite, and animation calls operate only on
the entity that owns the script and report a runtime error when the required
component is absent. `play_animation` returns `false` when the requested clip
does not exist.

Asset calls accept stable handles returned by `find_asset`, never raw absolute
paths. Audio and scene calls validate both asset readiness and asset type before
invoking a platform service. Operations that can fail return `success` and an
optional error message. Volumes are normalized to `0..1`, and sound pitch must
be positive.

Display information reports physical pixels, DPI, safe-area insets, project
reference resolution, window mode, and VSync. Physical display changes are
disabled while the editor window is visible, so scripts must handle a failed
`set_window_mode` or `set_vsync` result during editor Play Mode. Standalone
runtime applies them through `DisplayManager`.

`load_scene` accepts only a ready Scene asset. Loading is deferred until the
current scene update and physics callbacks have completed. The loaded runtime
scene is not inserted into the authoring scene list, and Stop always restores
the original authoring snapshot.

## Runtime events

Scripts may implement these optional callbacks:

```luau
on_collision_enter(context, other_entity, normal_x, normal_y, is_trigger)
on_collision_exit(context, other_entity, is_trigger)
on_ui_button_clicked(context, button_entity)
on_ui_focus_changed(context, previous_entity, focused_entity)
on_event(context, name, payload, source_entity)
```

Collision callbacks are delivered only to scripts on the two participants; the
contact normal is inverted for the second participant. UI and custom events are
broadcast to enabled script instances. Engine events are queued during system
work and delivered after the scene update so a callback may safely mutate
physics state. `emit_event` uses the owning entity as `source_entity`, and events
emitted by a callback are processed on the next dispatch boundary.

`Assets/Scripts/PlatformerPlayer.luau` in the external Platformer project is the
first authored gameplay example.
It drives the saved platformer player's horizontal velocity, jump, facing, and
Idle/Run/Jump/Fall clips. The scene owns its speed and input names as editable
public fields. Collision and audio bindings now allow the temporary trampoline
behavior and generated jump sound to move into project scripts. `GenericScene`
remains only as the transitional handler until that external project content is
migrated.

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

## Hot reload

Editor Play Mode watches the source stamp of every running Script asset before
each update. Saving a changed `.luau` file compiles it once and prepares a fresh
instance for every entity using that asset. A successful reload resets module
locals, rebuilds `fields` from the current `ScriptComponent`, runs the new
`on_create`, then calls `on_destroy` on the previous instance before releasing
it. Standalone builds leave source watching disabled.

Syntax, module-load, or `on_create` failures keep the previous instance running
with its existing module state. The Inspector reports that the previous version
was retained and shows the latest reload error. A later valid save retries and
clears that diagnostic. Runtime errors from normal `on_update` or event callbacks
still put that entity in the regular error state rather than rolling back code.

## Play Mode apply-back

Stop always restores the pristine authoring snapshot first. Immediately before
script teardown, the editor captures serializable runtime values and compares
them with the snapshot by entity, component, and leaf property. The Inspector
then offers a `Review Apply-Back` panel; every difference starts unselected.

`Apply Selected` copies only checked properties into the restored scene and
records the result as one atomic Undo/Redo command. `Discard All`, switching the
authoring scene, or starting Play again clears the pending review. Entity and
component creation/removal are intentionally excluded: this contract applies
properties shared by the runtime and authoring entity. Changes from a runtime
scene loaded through `load_scene` are also excluded because they do not belong
to the original authoring scene.

Engine2D intentionally has no embedded script editor. Open the external game
project in VSCode and use the Luau Language Server; the Inspector remains the
authoring surface for serialized public field values.

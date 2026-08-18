# Runtime Display Settings

`DisplayManager` owns platform display operations. Game code owns the settings UI,
confirmation flow, and persistence. Project display settings are defaults and must
not be modified to store a player's preferences.

The project authoring reference is not a desktop resolution. It defines game-space
composition and the internal reference used by cameras and presentation. Window
size, fullscreen mode, monitor resolution, framebuffer pixels, and DPI are managed
separately by `DisplayManager`. Changing a player's window resolution must not
change the project authoring reference.

## Build a settings menu

Query capabilities before showing controls:

```cpp
const Engine::DisplayCapabilities caps = get_display().capabilities();
const std::vector<Engine::DisplayInfo> displays = get_display().available_displays();

if (caps.can_change_resolution) {
    // Show window size and exclusive-mode resolution controls.
}
if (caps.can_change_display) {
    // Show the monitor selector. DisplayId values are session-only.
}
```

Load portable player preferences over the project defaults. The game owns these
keys and decides when to save them:

```cpp
const Engine::ProjectDisplaySettings& defaults = get_project_settings().display();
Engine::DisplaySettings candidate = get_display().current_state().settings;
candidate.window_mode = static_cast<Engine::WindowMode>(
    Engine::PlayerPrefs::get_int("display.window_mode", static_cast<int>(defaults.default_window_mode))
);
candidate.window_width = Engine::PlayerPrefs::get_int("display.window_width", defaults.default_window_width);
candidate.window_height = Engine::PlayerPrefs::get_int("display.window_height", defaults.default_window_height);
candidate.refresh_rate = Engine::PlayerPrefs::get_float("display.refresh_rate", defaults.default_refresh_rate);
candidate.vsync = Engine::PlayerPrefs::get_bool("display.vsync", defaults.default_vsync);
```

Apply a complete candidate configuration on the main thread:

```cpp
Engine::DisplaySettings candidate = get_display().current_state().settings;
candidate.window_mode = Engine::WindowMode::BorderlessFullscreen;
candidate.display_id = selected_display;
candidate.vsync = true;

const Engine::DisplayState previous = get_display().current_state();
const Engine::DisplayApplyResult result = get_display().apply(candidate);
if (!result.success) {
    show_display_error(result.message);
}
```

Physical display changes are intentionally unavailable while the integrated editor
is visible. Use Project Settings and Game View to preview output there. The same
call works when running standalone or after hiding the editor.

## Confirm and restore

Keep the previous settings while the game shows its confirmation countdown:

```cpp
const Engine::DisplaySettings previous = get_display().current_state().settings;
const Engine::DisplayApplyResult applied = get_display().apply(candidate);

if (applied.success && !player_confirmed_before_timeout()) {
    const Engine::DisplayApplyResult restored = get_display().apply(previous);
    if (!restored.success) {
        show_display_error(restored.message);
    }
}
```

`DisplayManager` already rolls back platform failures that happen during one
`apply()` call. The confirmation flow above handles a valid setting that the player
does not want to keep.

## Persist player preferences

Use `PlayerPrefs` explicitly. Store portable values such as window mode, logical
window size, refresh rate, and VSync. Do not persist `DisplayId`; connected-display
IDs are valid only during the current process. Resolve a preferred display again
at startup and fall back to the primary display.

```cpp
Engine::PlayerPrefs::set_int("display.window_mode", static_cast<int>(candidate.window_mode));
Engine::PlayerPrefs::set_int("display.window_width", candidate.window_width);
Engine::PlayerPrefs::set_int("display.window_height", candidate.window_height);
Engine::PlayerPrefs::set_float("display.refresh_rate", candidate.refresh_rate);
Engine::PlayerPrefs::set_bool("display.vsync", candidate.vsync);
Engine::PlayerPrefs::save();
```

In windowed mode, width and height are logical window coordinates. Inspect
`DisplayState::pixel_width`, `pixel_height`, and `dpi_scale` for the actual render
surface. Internal render resolution and render scale are separate systems and are
not part of this API yet.

## Mobile behavior

On mobile, the operating system owns the physical resolution and window mode.
Games should hide unsupported controls using `DisplayCapabilities` and consume the
read-only size, DPI, and orientation in `DisplayState`. Surface and orientation
changes automatically update the active `RenderView`; Canvas safe areas and render
scale are planned as separate systems.

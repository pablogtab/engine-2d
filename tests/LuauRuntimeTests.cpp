#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/Input.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/AnimationComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "editor/scripting/ScriptAnalyzer.hpp"
#include "editor/scripting/ScriptTemplates.hpp"
#include "events/Event.hpp"
#include "events/EventDispatcher.hpp"
#include "scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_luau_runtime_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "assets/scripts/Movement.luau", R"(--!strict
local script = {
    fields = {
        { name = "speed", type = "number", default = 4.0 },
        { name = "active", type = "boolean", default = true },
        { name = "label", type = "string", default = "Mover" },
        { name = "offset", type = "vector2", default = {1.0, 2.0} },
        { name = "target", type = "entity", default = 0 },
        { name = "asset", type = "asset", default = nil },
    },
}
function script.on_create(context)
    local x, y, z = context.get_position()
    context.set_position(x, y + 1.0, z)
end
function script.on_update(context, delta_time)
    local x, y, z = context.get_position()
    context.set_position(x + context.fields.speed * delta_time, y, z)
end
function script.on_destroy(context)
    local x, _, z = context.get_position()
    context.set_position(x, 99.0, z)
end
return script
)");
    write_file(root / "assets/scripts/NoStrict.luau", "return {}\n");
    write_file(root / "assets/audio/Test.wav", "test audio asset\n");
    write_file(root / "assets/scenes/Next.scene", "{}\n");
    write_file(root / "assets/scripts/HotReload.luau", R"(--!strict
local script = { fields = {} }
function script.on_create(context)
    local x, _, z = context.get_position()
    context.set_position(x, 1.0, z)
end
function script.on_update(context, _delta_time)
    local x, y, z = context.get_position()
    context.set_position(x + 1.0, y, z)
end
function script.on_destroy(context)
    local x, y, _ = context.get_position()
    context.set_position(x, y, 1.0)
end
return script
)");
    write_file(root / "assets/scripts/RuntimeApi.luau", R"(--!strict
local script = { fields = {} }

local function require_value(condition, message)
    if not condition then
        error(message)
    end
end

function script.on_create(context)
    local audio = context.find_asset("assets/audio/Test.wav")
    require_value(audio ~= nil, "audio asset was not found")
    require_value(context.asset_exists(audio), "audio asset is unavailable")
    require_value(context.asset_path(audio) == "assets/audio/Test.wav", "unexpected audio path")
    require_value(context.asset_type(audio) == "audio", "unexpected audio type")
    require_value(context.find_asset("assets/missing.wav") == nil, "missing asset resolved")
    require_value(not context.asset_exists("not-a-handle"), "invalid handle resolved")

    local sound_ok, sound_error = context.play_sound(audio, 0.75, 1.25)
    require_value(sound_ok, sound_error or "sound failed")
    local music_ok, music_error = context.play_music(audio, 0.4, false)
    require_value(music_ok, music_error or "music failed")
    context.pause_music()
    context.resume_music()
    context.stop_music()
    context.set_volume("master", 0.8)
    context.set_volume("music", 0.6)
    context.set_volume("sfx", 0.7)
    require_value(math.abs(context.get_volume("master") - 0.8) < 0.001, "master volume mismatch")
    require_value(math.abs(context.get_volume("music") - 0.6) < 0.001, "music volume mismatch")
    require_value(math.abs(context.get_volume("sfx") - 0.7) < 0.001, "sfx volume mismatch")

    local display = context.get_display_info()
    require_value(display.width == 1920 and display.height == 1080, "display size mismatch")
    require_value(display.reference_width == 1280 and display.reference_height == 720, "reference size mismatch")
    require_value(display.window_mode == "windowed" and display.vsync, "display state mismatch")
    require_value(display.safe_area.left == 10 and display.safe_area.bottom == 40, "safe area mismatch")
    local mode_ok, mode_error = context.set_window_mode("borderless_fullscreen")
    require_value(mode_ok, mode_error or "window mode failed")
    local vsync_ok, vsync_error = context.set_vsync(false)
    require_value(vsync_ok, vsync_error or "vsync failed")

    local scene = context.find_asset("assets/scenes/Next.scene")
    require_value(scene ~= nil and context.asset_type(scene) == "scene", "scene asset mismatch")
    local invalid_sound_ok, invalid_sound_error = context.play_sound(scene)
    require_value(not invalid_sound_ok and invalid_sound_error ~= nil, "scene played as audio")
    local invalid_mode_ok, invalid_mode_error = context.set_window_mode("invalid")
    require_value(not invalid_mode_ok and invalid_mode_error ~= nil, "invalid window mode applied")
    local scene_ok, scene_error = context.load_scene(scene)
    require_value(scene_ok, scene_error or "scene load failed")
    require_value(context.get_scene_name() == "RuntimeApi", "scene name mismatch")
    context.emit_event("api_ready", "payload")
end

function script.on_collision_enter(context, other, normal_x, normal_y, is_trigger)
    local x, y, z = context.get_position()
    context.set_position(x + other + normal_x + normal_y + (is_trigger and 1 or 0), y, z)
end

function script.on_collision_exit(context, other, is_trigger)
    local x, y, z = context.get_position()
    context.set_position(x, y + other + (is_trigger and 1 or 0), z)
end

function script.on_ui_button_clicked(context, button)
    local x, y, z = context.get_position()
    context.set_position(x, y, z + button)
end

function script.on_ui_focus_changed(context, previous, focused)
    local x, y, z = context.get_position()
    context.set_position(x, y + previous + focused, z)
end

function script.on_event(context, name, payload, source)
    if name == "api_ready" and payload == "payload" then
        local x, y, z = context.get_position()
        context.set_position(x, y, z + source)
    end
end

return script
)");
    const std::string platformer_source = FileSystem::read_text(
        std::string(ENGINE_SOURCE_DIR) + "/tests/fixtures/scripts/PlatformerPlayer.luau"
    );
    assert(!platformer_source.empty());
    write_file(root / "assets/scripts/PlatformerPlayer.luau", platformer_source);
    AssetDatabase database(root);
    const AssetScanResult scan = database.scan(root / "assets");
    assert(scan.ready == 7);
    const AssetRecord* movement = database.find_by_path("assets/scripts/Movement.luau");
    const AssetRecord* no_strict = database.find_by_path("assets/scripts/NoStrict.luau");
    const AssetRecord* platformer = database.find_by_path("assets/scripts/PlatformerPlayer.luau");
    const AssetRecord* runtime_api = database.find_by_path("assets/scripts/RuntimeApi.luau");
    const AssetRecord* hot_reload = database.find_by_path("assets/scripts/HotReload.luau");
    assert(movement && movement->type == AssetType::Script);
    assert(no_strict && no_strict->type == AssetType::Script);
    assert(platformer && platformer->type == AssetType::Script);
    assert(runtime_api && runtime_api->type == AssetType::Script);
    assert(hot_reload && hot_reload->type == AssetType::Script);

    const ScriptInspectionResult inspection = ScriptRuntime::inspect_asset(movement->handle, database);
    assert(inspection.success);
    assert(inspection.public_fields.size() == 6);
    assert(!ScriptRuntime::inspect_asset(no_strict->handle, database).success);
    const ScriptInspectionResult platformer_inspection =
        ScriptRuntime::inspect_asset(platformer->handle, database);
    assert(platformer_inspection.success);
    assert(ScriptAnalyzer::analyze_source(platformer_source, "PlatformerPlayer").success);

    EntityManager entities;
    const EntityID entity = entities.create_entity();
    entities.add_component<TransformComponent>(entity, TransformComponent{});
    ScriptComponent component;
    component.script_handle = movement->handle;
    component.public_fields = inspection.public_fields;
    std::get<double>(component.public_fields.front().value) = 10.0;
    entities.add_component<ScriptComponent>(entity, component);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entities, database);
    assert(started.started == 1 && started.failed == 0);
    assert(runtime.instance_count() == 1);
    assert(entities.get_component<TransformComponent>(entity).position.y == 1.0f);
    runtime.update(0.5f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 5.0f);
    assert(runtime.status(entity) && runtime.status(entity)->state == ScriptExecutionState::Running);
    runtime.shutdown();
    assert(entities.get_component<TransformComponent>(entity).position.y == 99.0f);

    Input::map_axis("Horizontal", AxisBinding{
        SDL_SCANCODE_A,
        SDL_SCANCODE_D,
        GamepadAxis::LEFT_X,
        false,
        {SDL_SCANCODE_LEFT},
        {SDL_SCANCODE_RIGHT}
    });
    SDL_Event key_down{};
    key_down.type = SDL_EVENT_KEY_DOWN;
    key_down.key.scancode = SDL_SCANCODE_RIGHT;
    Input::process_event(key_down);

    const EntityID platformer_entity = entities.create_entity();
    entities.add_component<TransformComponent>(platformer_entity, TransformComponent{});
    entities.add_component<RigidBodyComponent>(platformer_entity, RigidBodyComponent{});
    entities.add_component<SpriteComponent>(platformer_entity, SpriteComponent{});
    AnimationComponent animation;
    animation.add_clip("Idle", 0, 1);
    animation.add_clip("Run", 1, 1);
    animation.add_clip("Jump", 2, 1);
    animation.add_clip("Fall", 3, 1);
    entities.add_component<AnimationComponent>(platformer_entity, animation);
    ScriptComponent platformer_component;
    platformer_component.script_handle = platformer->handle;
    platformer_component.public_fields = platformer_inspection.public_fields;
    entities.add_component<ScriptComponent>(platformer_entity, platformer_component);

    const ScriptRuntimeStartResult platformer_started = runtime.start(entities, database);
    assert(platformer_started.started == 2 && platformer_started.failed == 0);
    runtime.update(1.0f / 60.0f);
    assert(entities.get_component<RigidBodyComponent>(platformer_entity).velocity.x == 280.0f);
    assert(!entities.get_component<SpriteComponent>(platformer_entity).flip_x);
    assert(runtime.status(platformer_entity));
    assert(runtime.status(platformer_entity)->state == ScriptExecutionState::Running);
    auto& live_fields = entities.get_component<ScriptComponent>(platformer_entity).public_fields;
    const auto walk_speed = std::find_if(
        live_fields.begin(), live_fields.end(),
        [](const ScriptPublicField& field) { return field.name == "walk_speed"; }
    );
    assert(walk_speed != live_fields.end());
    walk_speed->value = 840.0;
    runtime.update(1.0f / 60.0f);
    assert(entities.get_component<RigidBodyComponent>(platformer_entity).velocity.x == 840.0f);
    runtime.shutdown();
    Input::reset_states();

    EntityManager reload_entities;
    const EntityID reload_entity = reload_entities.create_entity();
    reload_entities.add_component<TransformComponent>(reload_entity, TransformComponent{});
    ScriptComponent reload_component;
    reload_component.script_handle = hot_reload->handle;
    reload_entities.add_component<ScriptComponent>(reload_entity, reload_component);
    ScriptRuntimeServices reload_services;
    reload_services.hot_reload_enabled = true;
    const ScriptRuntimeStartResult reload_started = runtime.start(
        reload_entities, database, nullptr, std::move(reload_services)
    );
    assert(reload_started.started == 1 && reload_started.failed == 0);
    runtime.update(0.0f);
    assert(reload_entities.get_component<TransformComponent>(reload_entity).position.x == 1.0f);

    write_file(root / "assets/scripts/HotReload.luau", "--!strict\nlocal broken: number =\n");
    const ScriptHotReloadResult failed_reload = runtime.reload_changed_scripts(true);
    assert(failed_reload.reloaded == 0 && failed_reload.failed == 1);
    assert(runtime.reload_error(hot_reload->handle));
    assert(runtime.status(reload_entity));
    assert(runtime.status(reload_entity)->state == ScriptExecutionState::Running);
    runtime.update(0.0f);
    assert(reload_entities.get_component<TransformComponent>(reload_entity).position.x == 2.0f);

    write_file(root / "assets/scripts/HotReload.luau", R"(--!strict
local script = { fields = {} }
function script.on_create(context)
    local x, _, z = context.get_position()
    context.set_position(x, 2.0, z)
end
function script.on_update(context, _delta_time)
    local x, y, z = context.get_position()
    context.set_position(x + 10.0, y, z)
end
return script
)");
    const ScriptHotReloadResult successful_reload = runtime.reload_changed_scripts(true);
    assert(successful_reload.reloaded == 1 && successful_reload.failed == 0);
    assert(!runtime.reload_error(hot_reload->handle));
    const glm::vec3 reloaded_position =
        reload_entities.get_component<TransformComponent>(reload_entity).position;
    assert(reloaded_position.y == 2.0f && reloaded_position.z == 1.0f);
    runtime.update(0.0f);
    assert(reload_entities.get_component<TransformComponent>(reload_entity).position.x == 12.0f);

    write_file(root / "assets/scripts/HotReload.luau", R"(--!strict
local script = { fields = {} }
function script.on_create(_context)
    error("reload initialization failed")
end
return script
)");
    const ScriptHotReloadResult runtime_failed_reload = runtime.reload_changed_scripts(true);
    assert(runtime_failed_reload.reloaded == 0 && runtime_failed_reload.failed == 1);
    assert(runtime.reload_error(hot_reload->handle));
    assert(runtime.status(reload_entity)->state == ScriptExecutionState::Running);
    runtime.update(0.0f);
    assert(reload_entities.get_component<TransformComponent>(reload_entity).position.x == 22.0f);

    write_file(root / "assets/scripts/HotReload.luau", R"(--!strict
local script = { fields = {} }
function script.on_update(context, _delta_time)
    local x, y, z = context.get_position()
    context.set_position(x + 100.0, y, z)
end
return script
)");
    const ScriptHotReloadResult recovered_reload = runtime.reload_changed_scripts(true);
    assert(recovered_reload.reloaded == 1 && recovered_reload.failed == 0);
    assert(!runtime.reload_error(hot_reload->handle));
    runtime.update(0.0f);
    assert(reload_entities.get_component<TransformComponent>(reload_entity).position.x == 122.0f);
    runtime.shutdown();

    EntityManager api_entities;
    const EntityID api_entity = api_entities.create_entity();
    const EntityID other_entity = api_entities.create_entity();
    api_entities.add_component<TransformComponent>(api_entity, TransformComponent{});
    ScriptComponent api_component;
    api_component.script_handle = runtime_api->handle;
    api_entities.add_component<ScriptComponent>(api_entity, api_component);

    int sound_calls = 0;
    int music_calls = 0;
    int pause_calls = 0;
    int resume_calls = 0;
    int stop_calls = 0;
    float master_volume = 1.0f;
    float music_volume = 1.0f;
    float sfx_volume = 1.0f;
    std::string loaded_scene;
    std::optional<WindowMode> requested_mode;
    std::optional<bool> requested_vsync;
    ScriptRuntimeServices services;
    services.play_sound = [&](const std::string& path, float volume, float pitch, std::string&) {
        assert(path == (root / "assets/audio/Test.wav").string());
        assert(volume == 0.75f && pitch == 1.25f);
        ++sound_calls;
        return true;
    };
    services.play_music = [&](const std::string& path, float volume, bool loop, std::string&) {
        assert(path == (root / "assets/audio/Test.wav").string());
        assert(volume == 0.4f && !loop);
        ++music_calls;
        return true;
    };
    services.pause_music = [&]() { ++pause_calls; };
    services.resume_music = [&]() { ++resume_calls; };
    services.stop_music = [&]() { ++stop_calls; };
    services.set_master_volume = [&](float value) { master_volume = value; };
    services.set_music_volume = [&](float value) { music_volume = value; };
    services.set_sfx_volume = [&](float value) { sfx_volume = value; };
    services.get_master_volume = [&]() { return master_volume; };
    services.get_music_volume = [&]() { return music_volume; };
    services.get_sfx_volume = [&]() { return sfx_volume; };
    services.query_display = []() {
        return ScriptDisplayInfo{
            .pixel_width = 1920,
            .pixel_height = 1080,
            .dpi_scale = 2.0f,
            .safe_area = {10.0f, 20.0f, 30.0f, 40.0f},
            .window_mode = WindowMode::Windowed,
            .vsync = true,
            .reference_width = 1280,
            .reference_height = 720
        };
    };
    services.apply_display = [&](const ScriptDisplayRequest& request, std::string&) {
        if (request.window_mode) requested_mode = request.window_mode;
        if (request.vsync) requested_vsync = request.vsync;
        return true;
    };
    services.request_scene_load = [&](const std::string& path, std::string&) {
        loaded_scene = path;
        return true;
    };
    services.current_scene_name = []() { return std::string{"RuntimeApi"}; };

    EventDispatcher dispatcher;
    const ScriptRuntimeStartResult api_started = runtime.start(
        api_entities, database, &dispatcher, std::move(services)
    );
    assert(api_started.started == 1 && api_started.failed == 0);
    assert(sound_calls == 1 && music_calls == 1);
    assert(pause_calls == 1 && resume_calls == 1 && stop_calls == 1);
    assert(master_volume == 0.8f && music_volume == 0.6f && sfx_volume == 0.7f);
    assert(requested_mode == WindowMode::BorderlessFullscreen);
    assert(requested_vsync.has_value() && !*requested_vsync);
    assert(loaded_scene == "assets/scenes/Next.scene");

    dispatcher.publish(CollisionEvent{api_entity, other_entity, {3.0f, 4.0f}, true});
    dispatcher.publish(CollisionEndEvent{api_entity, other_entity, true});
    dispatcher.publish(UIButtonClickedEvent{8});
    dispatcher.publish(UIFocusChangedEvent{11, 12});
    runtime.dispatch_events();
    const glm::vec3 event_position =
        api_entities.get_component<TransformComponent>(api_entity).position;
    assert(event_position.x == static_cast<float>(other_entity) + 8.0f);
    assert(event_position.y == static_cast<float>(other_entity) + 24.0f);
    assert(event_position.z == static_cast<float>(api_entity) + 8.0f);
    runtime.shutdown();

    int unsubscribe_calls = 0;
    const EventDispatcher::SubscriptionId subscription = dispatcher.subscribe<UIButtonClickedEvent>(
        [&](const UIButtonClickedEvent&) { ++unsubscribe_calls; }
    );
    dispatcher.publish(UIButtonClickedEvent{api_entity});
    assert(unsubscribe_calls == 1);
    assert(dispatcher.unsubscribe(subscription));
    assert(!dispatcher.unsubscribe(subscription));
    dispatcher.publish(UIButtonClickedEvent{api_entity});
    assert(unsubscribe_calls == 1);

    const EntityID invalid_entity = entities.create_entity();
    entities.add_component<TransformComponent>(invalid_entity, TransformComponent{});
    ScriptComponent invalid_component;
    invalid_component.script_handle = no_strict->handle;
    entities.add_component<ScriptComponent>(invalid_entity, invalid_component);
    const ScriptRuntimeStartResult restarted = runtime.start(entities, database);
    assert(restarted.started == 2 && restarted.failed == 1);
    assert(runtime.status(invalid_entity));
    assert(runtime.status(invalid_entity)->state == ScriptExecutionState::Error);
    runtime.shutdown();

    const ScriptAnalysisResult valid = ScriptAnalyzer::analyze_source(
        "--!strict\nlocal value: number = 1\nreturn { value = value }\n"
    );
    assert(valid.success);
    assert(ScriptAnalyzer::analyze_source(std::string(DEFAULT_SCRIPT_TEMPLATE)).success);
    const ScriptAnalysisResult invalid = ScriptAnalyzer::analyze_source(
        "--!strict\nlocal value: number = 'wrong'\nreturn { value = value }\n"
    );
    assert(!invalid.success && !invalid.diagnostics.empty());
    assert(!ScriptAnalyzer::analyze_source("return {}\n").success);

    fs::remove_all(root, error);
    std::cout << "LuauRuntimeTests passed\n";
    return 0;
}

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include "core/AssetDatabase.hpp"
#include "core/RuntimeAssetCache.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "events/Event.hpp"
#include "events/EventDispatcher.hpp"
#include "scripting/ScriptRuntime.hpp"
#include "scene/GameSession.hpp"
#include "enginescript/editor/EngineScriptWorkspace.hpp"
#include "enginescript/async/AsyncScheduler.hpp"

using namespace Engine;
using namespace EngineScript;

#include <filesystem>
#include <fstream>

namespace {
void write_test_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}
} // namespace

void test_play_mode_lifecycle() {
    std::cout << "[Test] PlayModeLifecycle_StartsAndUpdates... ";
    
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_enginescript_playmode_tests";
    std::error_code error;
    fs::remove_all(root, error);

    std::string code = R"(
@behaviour
export class PlayerController extends Behaviour {
    @expose({ min: 0.0, max: 500.0 })
    speed: Float = 220.0;

    @expose
    tickCount: Float = 0.0;

    @expose
    asyncCompleted: Bool = false;

    override on_start(): Void {
        compute_async_value();
    }

    override update(deltaTime: Float): Void {
        tickCount = tickCount + 1.0;
    }

    async compute_async_value(): Task<Int> {
        await nextFrame();
        asyncCompleted = true;
        return 7;
    }
}
)";
    write_test_file(root / "Assets/scripts/PlayerController.e2s", code);

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));

    const AssetRecord* record = asset_database.find_by_path(root / "Assets/scripts/PlayerController.e2s");
    assert(record != nullptr);
    assert(record->handle.is_valid());
    RuntimeAssetCache asset_cache;
    std::string asset_error;
    assert(asset_cache.preload(asset_database, record->handle, nullptr, asset_error));
    assert(asset_cache.is_preloaded(record->handle));

    EntityManager entity_manager;
    ScriptRuntime runtime;

    EntityID entity = entity_manager.create_entity();
    ScriptComponent script_comp;
    script_comp.enabled = true;
    script_comp.script_handle = record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script_comp);

    ScriptRuntimeServices services;
    auto start_res = runtime.start(entity_manager, asset_database, nullptr, services);
    assert(start_res.started == 1);
    assert(start_res.failed == 0);
    assert(runtime.instance_count() == 1);

    // Advance frames
    runtime.update(0.016f);
    runtime.update(0.016f);
    const auto& updated = entity_manager.get_component<ScriptComponent>(entity);
    const auto tick = std::find_if(updated.public_fields.begin(), updated.public_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "tickCount";
    });
    assert(tick != updated.public_fields.end());
    assert(std::get<double>(tick->value) == 2.0);
    const auto async_completed = std::find_if(updated.public_fields.begin(), updated.public_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "asyncCompleted";
    });
    assert(async_completed != updated.public_fields.end());
    assert(std::get<bool>(async_completed->value));
    
    runtime.shutdown();
    assert(runtime.instance_count() == 0);
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_async_scheduler_cancellation() {
    std::cout << "[Test] AsyncScheduler_CancelsOnDestroy... ";
    
    AsyncScheduler scheduler;
    bool task_completed = false;
    uint32_t entity_scope = 42;

    scheduler.schedule_delay(0.1f, [&]() {
        task_completed = true;
    }, entity_scope);

    assert(scheduler.pending_tasks_count() == 1);

    // Cancel scope before delay expires
    scheduler.cancel_scope(entity_scope);
    scheduler.step(0.2f, 0.2f);

    assert(!task_completed);
    assert(scheduler.pending_tasks_count() == 0);

    std::cout << "PASSED\n";
}

void test_game_session_transitions() {
    std::cout << "[Test] GameSession_TracksTransitions... ";

    GameSession session;
    std::string error;
    assert(!session.request_scene("", error));
    assert(session.state() == GameSession::TransitionState::Idle);
    assert(session.request_scene("Assets/Scenes/Level2.scene", error));
    assert(session.state() == GameSession::TransitionState::Requested);
    assert(session.path() == "Assets/Scenes/Level2.scene");
    session.complete_loaded();
    assert(session.state() == GameSession::TransitionState::Loaded);
    session.reset();
    assert(session.request_scene("Assets/Scenes/Missing.scene", error));
    session.complete_failed("missing scene");
    assert(session.state() == GameSession::TransitionState::Failed);
    assert(session.error() == "missing scene");
    session.cancel();
    assert(session.state() == GameSession::TransitionState::Cancelled);

    std::cout << "PASSED\n";
}

void test_e2s_script_inspection() {
    std::cout << "[Test] ScriptInspection_InspectsE2SFields... ";
    
    EngineScriptWorkspace ws("");
    std::string script = R"(
@behaviour
export class Enemy extends Behaviour {
    @expose({ min: 1.0, max: 100.0 })
    health: Float = 100.0;

    @expose
    isAggressive: Bool = true;
}
)";
    ws.add_source_file("Enemy.e2s", script);
    auto res = ws.compile();
    assert(res.success);

    const auto& manifest = ws.metadata_manifest();
    assert(manifest.behaviours.size() == 1);
    assert(manifest.behaviours[0].name == "Enemy");
    assert(manifest.behaviours[0].properties.size() == 2);
    assert(manifest.behaviours[0].properties[0].name == "health");
    assert(manifest.behaviours[0].properties[1].name == "isAggressive");

    std::cout << "PASSED\n";
}

void test_dynamic_inspector_field_modifications() {
    std::cout << "[Test] DynamicInspectorFieldModifications_LiveUpdate... ";
    
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_dynamic_fields_tests";
    std::error_code error;
    fs::remove_all(root, error);

    std::string code = R"(
@behaviour
export class DynamicPlayer extends Behaviour {
    constructor(
        @component private body: RigidBody2D,
        @component private sprite: SpriteRenderer2D,
        @component private transform: Transform2D,
        @inject private time: Time
    ) {}

    @expose
    walkSpeed: Float = 280.0;

    @expose
    jumpSpeed: Float = 520.0;

    override update(deltaTime: Float): Void {
        body.velocity.x = walkSpeed;
        sprite.flipX = true;
        transform.position.x = time.deltaTime;
    }
}
)";
    write_test_file(root / "Assets/scripts/DynamicPlayer.e2s", code);

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));

    const AssetRecord* record = asset_database.find_by_path(root / "Assets/scripts/DynamicPlayer.e2s");
    assert(record != nullptr);

    EntityManager entity_manager;
    ScriptRuntime runtime;

    EntityID entity = entity_manager.create_entity();
    RigidBodyComponent body = RigidBodyComponent::Dynamic();
    entity_manager.add_component<RigidBodyComponent>(entity, body);
    entity_manager.add_component<SpriteComponent>(entity, SpriteComponent{});
    entity_manager.add_component<TransformComponent>(entity, TransformComponent{});

    ScriptComponent script_comp;
    script_comp.enabled = true;
    script_comp.script_handle = record->handle;
    script_comp.public_fields = {
        {"walkSpeed", ScriptFieldType::Number, 280.0},
        {"jumpSpeed", ScriptFieldType::Number, 520.0}
    };
    entity_manager.add_component<ScriptComponent>(entity, script_comp);

    ScriptRuntimeServices services;
    auto start_res = runtime.start(entity_manager, asset_database, nullptr, services);
    assert(start_res.started == 1);

    // Initial update
    runtime.update(0.016f);
    assert(std::abs(entity_manager.get_component<RigidBodyComponent>(entity).velocity.x - 280.0f) < 0.001f);
    assert(entity_manager.get_component<SpriteComponent>(entity).flip_x);
    assert(entity_manager.get_component<TransformComponent>(entity).position.x > 0.0f);

    // Modify speed dynamically in-game (as done via Inspector)
    auto& live_comp = entity_manager.get_component<ScriptComponent>(entity);
    live_comp.public_fields[0].value = 750.0;

    // Advance frame and verify update executes with modified live component
    runtime.update(0.016f);
    assert(std::abs(entity_manager.get_component<RigidBodyComponent>(entity).velocity.x - 750.0f) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_coin_pickup_speed_boost() {
    std::cout << "[Test] CoinPickup_BoostsSpeed... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_coin_boost_tests";
    std::error_code error;
    fs::remove_all(root, error);

    std::string code = R"(
@behaviour
export class BoostPlayer extends Behaviour {
    @expose
    walkSpeed: Float = 280.0;

    on_collision_enter(other: Entity): Void {
        if (!other.name.toLowerCase().includes("coin")) {
            return;
        }
        walkSpeed = walkSpeed + 75.0;
        other.destroy();
    }
}
)";
    write_test_file(root / "Assets/scripts/BoostPlayer.e2s", code);

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));

    const AssetRecord* record = asset_database.find_by_path(root / "Assets/scripts/BoostPlayer.e2s");
    assert(record != nullptr);

    EntityManager entity_manager;
    EventDispatcher dispatcher;
    ScriptRuntime runtime;

    EntityID player = entity_manager.create_entity();
    TagComponent player_tag;
    player_tag.tag = "Player_Knight";
    entity_manager.add_component<TagComponent>(player, player_tag);

    ScriptComponent script_comp;
    script_comp.enabled = true;
    script_comp.script_handle = record->handle;
    script_comp.public_fields = {
        {"walkSpeed", ScriptFieldType::Number, 280.0}
    };
    entity_manager.add_component<ScriptComponent>(player, script_comp);

    EntityID coin = entity_manager.create_entity();
    TagComponent coin_tag;
    coin_tag.tag = "Collectible_Coin_Gold";
    entity_manager.add_component<TagComponent>(coin, coin_tag);

    ScriptRuntimeServices services;
    auto start_res = runtime.start(entity_manager, asset_database, &dispatcher, services);
    assert(start_res.started == 1);

    // Simulate collision event between player and coin
    CollisionEvent collision(player, coin, glm::vec2(0.0f, 0.0f), true);
    dispatcher.publish(collision);

    // Dispatch events in runtime
    runtime.dispatch_events();

    // Verify speed was boosted by +75.0 (280 + 75 = 355)
    const auto& updated_comp = entity_manager.get_component<ScriptComponent>(player);
    assert(std::holds_alternative<double>(updated_comp.public_fields[0].value));
    assert(std::abs(std::get<double>(updated_comp.public_fields[0].value) - 355.0) < 0.001);
    assert(!entity_manager.is_alive(coin));

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_audio_and_scene_services() {
    std::cout << "[Test] AudioAndSceneServices... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_enginescript_services_tests";
    std::error_code error;
    fs::remove_all(root, error);

    const std::string code = R"(
@behaviour
export class ServiceUser extends Behaviour {
    constructor(
        @inject private audio: Audio,
        @inject private scene: Scene,
        @inject private display: Display,
        @inject private events: Events,
        @inject private assets: Assets,
        @inject private entities: Entities,
        @inject private ui: UI
    ) {}

    @expose
    received: String = "";

    override on_start(): Void {
        audio.setMasterVolume(0.65);
        audio.playSound("Assets/Audio/start.wav", 0.5, 1.25);
        audio.playMusic("Assets/Audio/theme.ogg", 0.8, true);
        scene.load("Assets/Scenes/Level2.scene");
        display.setVsync(display.pixelWidth == 1920);
        display.setFullscreen(false);
        events.emit("PlayerReady", "level-2");
        events.subscribe("PlayerReady");
        events.subscribe("AssetLoaded");
        if (assets.exists("Assets/scripts/ServiceUser.e2s")) {
            events.emit("AssetReady", "script");
        }
        assets.request("Assets/scripts/ServiceUser.e2s");
        let started: Result<Int, String> = Result.success<Int, String>(7);
        let rejected: Result<Int, String> = Result.failure<Int, String>("not ready");
        loadHudTexture();
        let status: Entity = entities.findByName("Status");
        ui.setText(status, "Ready");
        ui.setVisible(status, true);
        instantiate("Transient");
    }

    async loadHudTexture(): Task<Void> {
        let loaded: Asset = await assets.load<Texture>("Assets/Textures/Hud.png");
        if (loaded.ready) {
            events.emit("TypedAssetLoaded", loaded.path);
        }
    }

    on_event(name: String, payload: String): Void {
        received = name + ":" + payload;
    }
}
)";
    write_test_file(root / "Assets/scripts/ServiceUser.e2s", code);
    write_test_file(root / "Assets/Textures/Hud.png", "texture-bytes");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* record = asset_database.find_by_path(root / "Assets/scripts/ServiceUser.e2s");
    assert(record != nullptr);
    const AssetRecord* texture_record = asset_database.find_by_path(root / "Assets/Textures/Hud.png");
    assert(texture_record != nullptr);

    EntityManager entity_manager;
    const EntityID entity = entity_manager.create_entity();
    ScriptComponent component;
    component.enabled = true;
    component.script_handle = record->handle;
    entity_manager.add_component<ScriptComponent>(entity, component);
    const EntityID status = entity_manager.create_entity();
    entity_manager.add_component<TagComponent>(status, TagComponent{"Status"});
    entity_manager.add_component<UITextComponent>(status, UITextComponent{});

    int played_sounds = 0;
    int played_music = 0;
    float master_volume = 0.0f;
    std::string loaded_scene;
    int display_changes = 0;
    int script_events = 0;
    ScriptRuntimeServices services;
    services.play_sound = [&](const std::string& path, float volume, float pitch, std::string&) {
        assert(path == "Assets/Audio/start.wav");
        assert(std::abs(volume - 0.5f) < 0.001f);
        assert(std::abs(pitch - 1.25f) < 0.001f);
        ++played_sounds;
        return true;
    };
    services.play_music = [&](const std::string& path, float volume, bool loop, std::string&) {
        assert(path == "Assets/Audio/theme.ogg");
        assert(std::abs(volume - 0.8f) < 0.001f);
        assert(loop);
        ++played_music;
        return true;
    };
    services.set_master_volume = [&](float volume) { master_volume = volume; };
    services.request_scene_load = [&](const std::string& path, std::string&) {
        loaded_scene = path;
        return true;
    };
    services.preload_asset = [&](AssetHandle handle, std::string&) {
        return handle == record->handle || handle == texture_record->handle;
    };
    services.query_display = []() {
        ScriptDisplayInfo info;
        info.pixel_width = 1920;
        info.pixel_height = 1080;
        info.reference_width = 960;
        info.reference_height = 540;
        info.dpi_scale = 2.0f;
        info.window_mode = WindowMode::BorderlessFullscreen;
        info.vsync = true;
        return info;
    };
    services.apply_display = [&](const ScriptDisplayRequest& request, std::string&) {
        if (display_changes == 0) assert(request.vsync && *request.vsync);
        if (display_changes == 1) assert(request.window_mode && *request.window_mode == WindowMode::Windowed);
        ++display_changes;
        return true;
    };

    EventDispatcher dispatcher;
    dispatcher.subscribe<ScriptEvent>([&](const ScriptEvent& event) {
        assert(event.source == entity);
        if (event.name == "PlayerReady") assert(event.payload == "level-2");
        else if (event.name == "AssetReady") {
            assert(event.name == "AssetReady");
            assert(event.payload == "script");
        } else {
            assert(event.name == "AssetLoaded" || event.name == "TypedAssetLoaded");
            assert(event.payload == "Assets/scripts/ServiceUser.e2s" || event.payload == "Assets/Textures/Hud.png");
        }
        ++script_events;
    });

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, &dispatcher, services);
    assert(started.started == 1);
    assert(started.failed == 0);
    assert(played_sounds == 1);
    assert(played_music == 1);
    assert(std::abs(master_volume - 0.65f) < 0.001f);
    assert(loaded_scene == "Assets/Scenes/Level2.scene");
    assert(display_changes == 2);
    assert(entity_manager.get_component<UITextComponent>(status).text == "Ready");
    bool transient_exists = false;
    entity_manager.for_each<TagComponent>([&](EntityID, const TagComponent& tag) {
        transient_exists = transient_exists || tag.tag == "Transient";
    });
    assert(transient_exists);
    runtime.update(0.0f);
    dispatcher.dispatch_queued();
    runtime.dispatch_events();
    runtime.update(0.0f);
    dispatcher.dispatch_queued();
    runtime.dispatch_events();
    assert(script_events == 4);
    const ScriptComponent& updated_component = entity_manager.get_component<ScriptComponent>(entity);
    const auto received = std::find_if(updated_component.public_fields.begin(), updated_component.public_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "received";
    });
    assert(received != updated_component.public_fields.end());
    assert(std::get<std::string>(received->value) == "AssetLoaded:Assets/scripts/ServiceUser.e2s");

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running EngineScript Play Mode Tests ===\n";
    test_play_mode_lifecycle();
    test_dynamic_inspector_field_modifications();
    test_coin_pickup_speed_boost();
    test_audio_and_scene_services();
    test_async_scheduler_cancellation();
    test_game_session_transitions();
    test_e2s_script_inspection();
    std::cout << "=== All EngineScript Play Mode Tests Passed! ===\n";
    return 0;
}

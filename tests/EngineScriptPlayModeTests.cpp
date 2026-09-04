#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include "core/AssetDatabase.hpp"
#include "core/RuntimeAssetCache.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/TransformSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/ParticleComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "events/Event.hpp"
#include "events/EventDispatcher.hpp"
#include "physics/PhysicsSystem.hpp"
#include "scripting/ScriptRuntime.hpp"
#include "scene/EntitySubtreeArchive.hpp"
#include "scene/GameSession.hpp"
#include "scene/SceneRuntime.hpp"
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
    script_comp.instances.front().enabled = true;
    script_comp.instances.front().script_handle = record->handle;
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
    const auto& updated_fields = updated.instances.front().public_fields;
    const auto tick = std::find_if(updated_fields.begin(), updated_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "tickCount";
    });
    assert(tick != updated_fields.end());
    assert(std::get<double>(tick->value) == 2.0);
    const auto async_completed = std::find_if(updated_fields.begin(), updated_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "asyncCompleted";
    });
    assert(async_completed != updated_fields.end());
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
        sprite.color = Color(1.0, 0.2, 0.3, 0.8);
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
    script_comp.instances.front().enabled = true;
    script_comp.instances.front().script_handle = record->handle;
    script_comp.instances.front().public_fields = {
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
    const SDL_Color sprite_color = entity_manager.get_component<SpriteComponent>(entity).color;
    assert(sprite_color.r == 255 && sprite_color.g == 51 && sprite_color.b == 77 && sprite_color.a == 204);
    assert(entity_manager.get_component<TransformComponent>(entity).position.x > 0.0f);

    // Modify speed dynamically in-game (as done via Inspector)
    auto& live_comp = entity_manager.get_component<ScriptComponent>(entity);
    live_comp.instances.front().public_fields[0].value = 750.0;

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
    constructor(@component private body: RigidBody2D) {}

    @expose
    walkSpeed: Float = 280.0;

    on_collision_enter(other: Entity): Void {
        if (!other.name.toLowerCase().includes("coin")) {
            return;
        }
        walkSpeed = walkSpeed + 75.0;
        body.velocity.y = 780.0;
        body.setIgnoreOneWay(true);
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
    entity_manager.add_component<RigidBodyComponent>(player, RigidBodyComponent::Dynamic());

    ScriptComponent script_comp;
    script_comp.instances.front().enabled = true;
    script_comp.instances.front().script_handle = record->handle;
    script_comp.instances.front().public_fields = {
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
    assert(std::holds_alternative<double>(updated_comp.instances.front().public_fields[0].value));
    assert(std::abs(std::get<double>(updated_comp.instances.front().public_fields[0].value) - 355.0) < 0.001);
    assert(std::abs(entity_manager.get_component<RigidBodyComponent>(player).velocity.y - 780.0f) < 0.001f);
    assert(PhysicsSystem::is_ignoring_one_way(player));
    assert(!entity_manager.is_alive(coin));

    PhysicsSystem::set_ignore_one_way(player, false);

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
        @inject private assets: Assets,
        @inject private entities: Entities,
        @inject private prefs: PlayerPrefs,
        @inject private ui: UI
    ) {}

    @expose
    tagChecks: Int = 0;

    override on_start(): Void {
        audio.setMasterVolume(0.65);
        audio.playSound("Assets/Audio/start.wav", 0.5, 1.25);
        audio.playMusic("Assets/Audio/theme.ogg", 0.8, true);
        scene.load("Assets/Scenes/Level2.scene");
        display.setVsync(display.pixelWidth == 1920);
        display.setFullscreen(false);
        let started: Result<Int, String> = Result.success<Int, String>(7);
        let rejected: Result<Int, String> = Result.failure<Int, String>("not ready");
        let points: Array<Int> = [1, 2, 3];
        if (started.isOk() && started.value() == points[0] + 6 && rejected.isError()) {}
        loadHudTexture();
        let status: Entity = entities.findByName("Status");
        ui.setText(status, "Ready");
        ui.setVisible(status, true);
        if (entities.addGameplayTag(status, "ui.status")) tagChecks = tagChecks + 1;
        if (entities.hasGameplayTag(status, "ui.status")) tagChecks = tagChecks + 1;
        let tagged: Entity = entities.findByGameplayTag("ui.status");
        if (entities.exists(tagged)) tagChecks = tagChecks + 1;
        if (entities.removeGameplayTag(status, "ui.status")) tagChecks = tagChecks + 1;
        instantiate("Transient");
    }

    async loadHudTexture(): Task<Void> {
        let result: Result<Asset<Texture>, String> = await assets.load<Texture>("Assets/Textures/Hud.png");
        if (result.isOk()) {
            let loaded: Asset<Texture> = result.value();
            if (loaded.ready) { ui.setVisible(entities.findByName("Status"), true); }
        }
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
    component.instances.front().enabled = true;
    component.instances.front().script_handle = record->handle;
    entity_manager.add_component<ScriptComponent>(entity, component);
    const EntityID status = entity_manager.create_entity();
    entity_manager.add_component<TagComponent>(status, TagComponent{"Status"});
    entity_manager.add_component<UITextComponent>(status, UITextComponent{});

    int played_sounds = 0;
    int played_music = 0;
    int stopped_music = 0;
    float master_volume = 0.0f;
    std::string loaded_scene;
    int display_changes = 0;
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
    services.stop_music = [&]() { ++stopped_music; };
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
    assert(entity_manager.has_component<GameplayTagsComponent>(status));
    assert(entity_manager.get_component<GameplayTagsComponent>(status).tags.empty());
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
    const ScriptComponent& updated_component = entity_manager.get_component<ScriptComponent>(entity);
    const auto& updated_fields = updated_component.instances.front().public_fields;
    const auto tag_checks = std::find_if(updated_fields.begin(), updated_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "tagChecks";
    });
    assert(tag_checks != updated_fields.end());
    assert(std::get<double>(tag_checks->value) == 4.0);

    runtime.shutdown();
    assert(stopped_music == 1);
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_particle_services_and_component() {
    std::cout << "[Test] ParticleServicesAndComponent... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_enginescript_particle_services_tests";
    std::error_code error;
    fs::remove_all(root, error);

    const std::string code = R"(
@behaviour
export class ParticleUser extends Behaviour {
    constructor(
        @inject private particles: Particles,
        @inject private assets: Assets,
        @component private emitter: ParticleEmitter2D,
        @component private transform: Transform2D
    ) {}

    override on_start(): Void { configureEffect(); }

    async configureEffect(): Task<Void> {
        let result: Result<Asset<ParticleEffect2D>, String> = await assets.load<ParticleEffect2D>("Assets/Particles/dust.particle");
        if (result.isOk()) {
            let effect: Asset<ParticleEffect2D> = result.value();
            if (!effect.ready || !emitter.setEffect(effect)) { return; }
            emitter.play();
            emitter.burst(6);
            particles.emit(effect, transform.position, 3);
            let options: ParticleEmitOptions = { count: 4, scale: 1.5 };
            particles.emitWithOptions(effect, transform.position, options);
            emitter.stop(true);
        }
    }
}
)";
    write_test_file(root / "Assets/Scripts/ParticleUser.e2s", code);
    write_test_file(root / "Assets/Particles/dust.particle", R"({"version":1,"type":"ParticleEffect2D","name":"Dust","appearance":{},"motion":{},"lifetime":{},"collision":{},"emission":{},"rendering":{}})");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/ParticleUser.e2s");
    const AssetRecord* effect_record = asset_database.find_by_path(root / "Assets/Particles/dust.particle");
    assert(script_record && effect_record);

    EntityManager entity_manager;
    const EntityID entity = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(entity, TransformComponent{});
    entity_manager.add_component<ParticleEmitter2DComponent>(entity, ParticleEmitter2DComponent{});
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script);

    int play_calls = 0;
    int burst_count = 0;
    int direct_count = 0;
    int options_count = 0;
    bool stopped_and_cleared = false;
    ScriptRuntimeServices services;
    services.preload_asset = [&](AssetHandle handle, std::string&) { return handle == effect_record->handle; };
    services.play_particle_emitter = [&](EntityID id) { assert(id == entity); ++play_calls; return true; };
    services.burst_particle_emitter = [&](EntityID id, int count) { assert(id == entity); burst_count += count; return true; };
    services.stop_particle_emitter = [&](EntityID id, bool clear) { assert(id == entity); stopped_and_cleared = clear; return true; };
    services.emit_particles = [&](AssetHandle handle, const glm::vec2&, int count) {
        assert(handle == effect_record->handle);
        direct_count += count;
        return true;
    };
    services.emit_particles_with_options = [&](AssetHandle handle, const glm::vec2&, const EngineScript::ParticleEmitOptions& options) {
        assert(handle == effect_record->handle);
        assert(std::abs(options.scale - 1.5f) < 0.001f);
        options_count += options.count;
        return true;
    };

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, nullptr, services);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.0f);
    runtime.update(0.0f);

    assert(entity_manager.get_component<ParticleEmitter2DComponent>(entity).effect_handle == effect_record->handle);
    assert(play_calls == 1);
    assert(burst_count == 6);
    assert(direct_count == 3);
    assert(options_count == 4);
    assert(stopped_and_cleared);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_collection_range_error_is_controlled() {
    std::cout << "[Test] CollectionRangeError_IsControlled... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_collection_range_error_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_test_file(root / "Assets/Scripts/RangeError.e2s", R"(
@behaviour
export class RangeError extends Behaviour {
    override update(deltaTime: Float): Void {
        let values: Array<Int> = [1];
        let missing: Int = values[2];
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/RangeError.e2s");
    assert(script_record);

    EntityManager entity_manager;
    const EntityID entity = entity_manager.create_entity();
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.016f);
    const ScriptExecutionStatus* status = runtime.status(entity);
    assert(status && status->state == ScriptExecutionState::Error);
    assert(status->message.find("collection index is out of range") != std::string::npos);

    // An errored behaviour is fail-stopped rather than invoked again.
    runtime.update(0.016f);
    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_world_text_service() {
    std::cout << "[Test] WorldTextService... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_world_text_service_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/DamageText.e2s", R"(
@behaviour
export class DamageText extends Behaviour {
    constructor(@inject private worldText: WorldText) {}

    override on_start(): Void {
        let options: FloatingTextOptions = {
            color: Color(0.25, 0.75, 1.0, 1.0),
            velocity: Vec2(0.0, 60.0),
            duration: 0.8,
            scale: 1.25
        };
        worldText.show("42", Vec2(12.0, 34.0), options);
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* record = asset_database.find_by_path(root / "Assets/Scripts/DamageText.e2s");
    assert(record != nullptr);

    EntityManager entity_manager;
    const EntityID entity = entity_manager.create_entity();
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script);

    int calls = 0;
    ScriptRuntimeServices services;
    services.show_world_text = [&calls](
        const std::string& text,
        const glm::vec2& position,
        const EngineScript::FloatingTextOptions& options
    ) {
        ++calls;
        assert(text == "42");
        assert(position == glm::vec2(12.0f, 34.0f));
        assert(std::abs(options.velocity.y - 60.0f) < 0.001f);
        assert(std::abs(options.duration - 0.8f) < 0.001f);
        assert(std::abs(options.scale - 1.25f) < 0.001f);
        return true;
    };

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, nullptr, services);
    assert(started.started == 1 && started.failed == 0);
    assert(calls == 1);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_world_text_queue_is_bounded() {
    std::cout << "[Test] WorldTextQueue_IsBounded... ";

    SceneRuntime runtime;
    assert(!runtime.show_world_text({.text = "", .duration = 0.75f}));
    for (int index = 0; index < 257; ++index) {
        assert(runtime.show_world_text({
            .text = std::to_string(index),
            .duration = 0.75f,
            .remaining = 0.75f
        }));
    }
    assert(runtime.world_texts().size() == 256);
    assert(runtime.world_texts().front().text == "1");
    assert(runtime.world_texts().back().text == "256");

    std::cout << "PASSED\n";
}

void test_entity_transform_following_across_updates() {
    std::cout << "[Test] EntityTransformFollowing_AcrossUpdates... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_entity_follow_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/Follower.e2s", R"(
@behaviour
export class Follower extends Behaviour {
    constructor(
        @inject private entities: Entities,
        @component private transform: Transform2D
    ) {}

    @expose
    speed: Float = 180.0;

    override update(deltaTime: Float): Void {
        let player: Entity = entities.findByGameplayTag("player");
        if (!entities.exists(player)) return;
        let playerTransform: Transform2D? = player.getComponent<Transform2D>();
        if (playerTransform != null) {
            let target: Vec2 = playerTransform.position;
            let difference: Vec2 = target - transform.position;
            let amount: Float = speed * deltaTime;
            if (difference.x > amount) transform.position.x = transform.position.x + amount;
            else transform.position.x = target.x;
        }
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/Follower.e2s");
    assert(script_record);

    EntityManager entity_manager;
    const EntityID player = entity_manager.create_entity();
    TransformComponent player_transform;
    player_transform.position.x = 10.0f;
    entity_manager.add_component<TransformComponent>(player, player_transform);
    entity_manager.add_component<GameplayTagsComponent>(player, GameplayTagsComponent{{"player"}});

    const EntityID follower = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(follower, TransformComponent{});
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    entity_manager.add_component<ScriptComponent>(follower, script);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database);
    assert(started.started == 1 && started.failed == 0);

    runtime.update(0.1f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(follower).position.x - 10.0f) < 0.001f);

    entity_manager.get_component<TransformComponent>(player).position.x = 20.0f;
    runtime.update(0.1f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(player).position.x - 20.0f) < 0.001f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(follower).position.x - 20.0f) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_member_initializer_keeps_component_field_reference() {
    std::cout << "[Test] MemberInitializer_KeepsComponentFieldReference... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_member_reference_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/AliasWriter.e2s", R"(
@behaviour
export class AliasWriter extends Behaviour {
    constructor(@inject private entities: Entities) {}

    override update(deltaTime: Float): Void {
        let player: Entity = entities.findByGameplayTag("player");
        let playerTransform: Transform2D? = player.getComponent<Transform2D>();
        if (!playerTransform) return;

        let original: Vec2 = playerTransform.position;
        original.x = 42.0;
    }
}

)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/AliasWriter.e2s");
    assert(script_record);

    EntityManager entity_manager;
    const EntityID player = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(player, TransformComponent{});
    entity_manager.add_component<GameplayTagsComponent>(player, GameplayTagsComponent{{"player"}});

    const EntityID writer = entity_manager.create_entity();
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    entity_manager.add_component<ScriptComponent>(writer, script);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.016f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(player).position.x - 42.0f) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_component_views_do_not_alias_between_entities() {
    std::cout << "[Test] ComponentViews_DoNotAliasBetweenEntities... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_component_view_alias_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/MultiTransformWriter.e2s", R"(
@behaviour
export class MultiTransformWriter extends Behaviour {
    constructor(@inject private entities: Entities) {}

    override update(deltaTime: Float): Void {
        let firstEntity: Entity = entities.findByGameplayTag("view.first");
        let secondEntity: Entity = entities.findByGameplayTag("view.second");
        if (entities.exists(firstEntity) && entities.hasTransform(firstEntity)) {
            let firstPosition: Vec2 = entities.transform(firstEntity).position;
            firstPosition.x = firstPosition.x + 11.0;
        }
        if (entities.exists(secondEntity) && entities.hasTransform(secondEntity)) {
            let secondPosition: Vec2 = entities.transform(secondEntity).position;
            secondPosition.x = secondPosition.x + 22.0;
        }
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/MultiTransformWriter.e2s");
    assert(script_record);

    EntityManager entity_manager;
    const EntityID first = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(first, TransformComponent{});
    entity_manager.add_component<GameplayTagsComponent>(first, GameplayTagsComponent{{"view.first"}});

    const EntityID second = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(second, TransformComponent{});
    entity_manager.add_component<GameplayTagsComponent>(second, GameplayTagsComponent{{"view.second"}});

    const EntityID writer = entity_manager.create_entity();
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    entity_manager.add_component<ScriptComponent>(writer, script);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.016f);

    assert(std::abs(entity_manager.get_component<TransformComponent>(first).position.x - 11.0f) < 0.001f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(second).position.x - 22.0f) < 0.001f);

    // A later callback must resolve fresh views instead of replaying stale snapshots.
    runtime.update(0.016f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(first).position.x - 22.0f) < 0.001f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(second).position.x - 44.0f) < 0.001f);

    // Destroyed components/entities are skipped safely by the next callback.
    entity_manager.destroy_entity(second);
    runtime.update(0.016f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(first).position.x - 33.0f) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_exposed_component_reference_targets_other_entity() {
    std::cout << "[Test] ExposedComponentReference_TargetsOtherEntity... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_exposed_component_reference_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/ReferenceWriter.e2s", R"(
@behaviour
export class ReferenceWriter extends Behaviour {
    @expose
    target: ComponentRef<Transform2D>;

    override update(deltaTime: Float): Void {
        if (!target) return;
        target.position.x = 37.0;
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/ReferenceWriter.e2s");
    assert(script_record);
    const ScriptInspectionResult inspected = ScriptRuntime::inspect_asset(script_record->handle, asset_database);
    assert(inspected.success && inspected.public_fields.size() == 1);
    assert(inspected.public_fields.front().name == "target");
    assert(inspected.public_fields.front().type == ScriptFieldType::Entity);

    EntityManager entity_manager;
    const EntityID knight = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(knight, TransformComponent{});

    const EntityID pet = entity_manager.create_entity();
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    script.instances.front().public_fields = {
        {"target", ScriptFieldType::Entity, knight}
    };
    entity_manager.add_component<ScriptComponent>(pet, script);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.016f);
    assert(std::abs(entity_manager.get_component<TransformComponent>(knight).position.x - 37.0f) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_coin_pickup_spawns_prefab_at_knight_position() {
    std::cout << "[Test] CoinPickup_SpawnsPrefabAtKnightPosition... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_prefab_spawn_position_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/PetSpawner.e2s", R"(
@behaviour
export class PetSpawner extends Behaviour {
    constructor(
        @inject private prefabs: Prefabs,
        @component private transform: Transform2D
    ) {}

    @expose
    petPrefab: Prefab;

    on_collision_enter(other: Entity): Void {
        let pet: Entity = prefabs.create(petPrefab);
        prefabs.spawn(pet, transform.position);
    }
}
)");
    write_test_file(root / "Assets/Scripts/PetBehaviour.e2s", R"(
@behaviour
export class PetBehaviour extends Behaviour {
    constructor(@component private transform: Transform2D) {}
}
)");
    write_test_file(root / "Assets/Prefabs/FlyingFox.prefab", "{}");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/PetSpawner.e2s");
    const AssetRecord* pet_script_record = asset_database.find_by_path(root / "Assets/Scripts/PetBehaviour.e2s");
    const AssetRecord* prefab_record = asset_database.find_by_path(root / "Assets/Prefabs/FlyingFox.prefab");
    assert(script_record && pet_script_record && prefab_record);

    EntityManager entity_manager;
    EventDispatcher dispatcher;
    ScriptRuntime runtime;

    const EntityID knight = entity_manager.create_entity();
    TransformComponent knight_transform;
    knight_transform.position = {137.0f, -42.0f, 0.0f};
    knight_transform.world_position = knight_transform.position;
    entity_manager.add_component<TransformComponent>(knight, knight_transform);
    ScriptComponent knight_script;
    knight_script.instances.front().enabled = true;
    knight_script.instances.front().script_handle = script_record->handle;
    knight_script.instances.front().public_fields = {
        {"petPrefab", ScriptFieldType::Asset, prefab_record->handle, {}, "Prefab"}
    };
    entity_manager.add_component<ScriptComponent>(knight, knight_script);

    const EntityID coin = entity_manager.create_entity();
    const EntityID pet = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(pet, TransformComponent{});
    ScriptComponent pet_script;
    // The staged instance is dormant until Prefabs.create prepares it.
    pet_script.instances.front().enabled = false;
    pet_script.instances.front().script_handle = pet_script_record->handle;
    entity_manager.add_component<ScriptComponent>(pet, pet_script);
    AssetHandle created_prefab;
    EntityID spawned_entity = 0;
    glm::vec2 spawned_position{};
    ScriptRuntimeServices services;
    services.create_prefab = [&](AssetHandle handle) {
        created_prefab = handle;
        entity_manager.get_component<ScriptComponent>(pet).instances.front().enabled = true;
        assert(runtime.prepare_entity(pet));
        return pet;
    };
    services.spawn_prefab = [&](EntityID entity, const glm::vec2& position) {
        spawned_entity = entity;
        spawned_position = position;
        TransformComponent& transform = entity_manager.get_component<TransformComponent>(entity);
        transform.position = {position.x, position.y, 0.0f};
        TransformSystem::update(entity_manager);
        return runtime.activate_entity(entity);
    };

    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, &dispatcher, services);
    assert(started.started == 1 && started.failed == 0);
    dispatcher.publish(CollisionEvent(knight, coin, glm::vec2{}, true));
    runtime.dispatch_events();

    assert(created_prefab == prefab_record->handle);
    assert(spawned_entity == pet);
    assert(std::abs(spawned_position.x - knight_transform.position.x) < 0.001f);
    assert(std::abs(spawned_position.y - knight_transform.position.y) < 0.001f);
    const TransformComponent& spawned_transform = entity_manager.get_component<TransformComponent>(pet);
    assert(std::abs(spawned_transform.position.x - knight_transform.position.x) < 0.001f);
    assert(std::abs(spawned_transform.position.y - knight_transform.position.y) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_update_spawns_prefab_at_moved_owner_position() {
    std::cout << "[Test] Update_SpawnsPrefabAtMovedOwnerPosition... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_update_prefab_spawn_position_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/ProjectileSpawner.e2s", R"(
@behaviour
export class ProjectileSpawner extends Behaviour {
    constructor(
        @inject private prefabs: Prefabs,
        @component private transform: Transform2D
    ) {}

    @expose
    projectilePrefab: Prefab;

    private fired: Bool = false;

    override update(deltaTime: Float): Void {
        if (fired) return;
        fired = true;
        let projectile: Entity = prefabs.create(projectilePrefab);
        prefabs.spawn(projectile, transform.position);
    }
}
)");
    write_test_file(root / "Assets/Scripts/ProjectileBehaviour.e2s", R"(
@behaviour
export class ProjectileBehaviour extends Behaviour {
    constructor(@component private transform: Transform2D) {}
}
)");
    write_test_file(root / "Assets/Prefabs/Projectile.prefab", "{}");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* spawner_script = asset_database.find_by_path(root / "Assets/Scripts/ProjectileSpawner.e2s");
    const AssetRecord* projectile_script = asset_database.find_by_path(root / "Assets/Scripts/ProjectileBehaviour.e2s");
    const AssetRecord* prefab_record = asset_database.find_by_path(root / "Assets/Prefabs/Projectile.prefab");
    assert(spawner_script && projectile_script && prefab_record);

    EntityManager entity_manager;
    ScriptRuntime runtime;
    const EntityID spawner = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(spawner, TransformComponent{});
    ScriptComponent spawner_component;
    spawner_component.instances.front().enabled = true;
    spawner_component.instances.front().script_handle = spawner_script->handle;
    spawner_component.instances.front().public_fields = {
        {"projectilePrefab", ScriptFieldType::Asset, prefab_record->handle, {}, "Prefab"}
    };
    entity_manager.add_component<ScriptComponent>(spawner, spawner_component);

    const EntityID projectile = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(projectile, TransformComponent{});
    RigidBodyComponent projectile_body;
    projectile_body.body_type = BodyType::Kinematic;
    projectile_body.gravity_scale = 0.0f;
    entity_manager.add_component<RigidBodyComponent>(projectile, projectile_body);
    ColliderComponent projectile_collider;
    ColliderShape projectile_shape;
    projectile_shape.type = ColliderShapeType::Circle;
    projectile_shape.radius = 7.0f;
    projectile_collider.shapes.push_back(projectile_shape);
    entity_manager.add_component<ColliderComponent>(projectile, projectile_collider);
    ScriptComponent projectile_component;
    projectile_component.instances.front().enabled = false;
    projectile_component.instances.front().script_handle = projectile_script->handle;
    entity_manager.add_component<ScriptComponent>(projectile, projectile_component);

    EventDispatcher dispatcher;
    PhysicsSystem::clear();
    TransformSystem::update(entity_manager);
    PhysicsSystem::update(entity_manager, dispatcher, 0.016f);

    glm::vec2 spawned_position{};
    ScriptRuntimeServices services;
    services.create_prefab = [&](AssetHandle) {
        entity_manager.get_component<ScriptComponent>(projectile).instances.front().enabled = true;
        PhysicsSystem::rebuild_body_hierarchy(entity_manager, projectile);
        runtime.queue_entity_start(projectile);
        assert(runtime.prepare_entity(projectile));
        return projectile;
    };
    services.spawn_prefab = [&](EntityID entity, const glm::vec2& position) {
        spawned_position = position;
        TransformComponent& target = entity_manager.get_component<TransformComponent>(entity);
        target.position = {position.x, position.y, 0.0f};
        target.world_position = target.position;
        TransformSystem::update(entity_manager);
        PhysicsSystem::sync_body_hierarchy(entity_manager, entity);
        return runtime.activate_entity(entity);
    };

    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, &dispatcher, services);
    assert(started.started == 1 && started.failed == 0);
    TransformComponent& moved = entity_manager.get_component<TransformComponent>(spawner);
    moved.position = {251.0f, -93.0f, 0.0f};
    moved.world_position = moved.position;

    runtime.update(0.016f);
    PhysicsSystem::update(entity_manager, dispatcher, 0.016f);

    assert(runtime.instance_count() == 2);

    assert(std::abs(spawned_position.x - moved.position.x) < 0.001f);
    assert(std::abs(spawned_position.y - moved.position.y) < 0.001f);
    const TransformComponent& spawned = entity_manager.get_component<TransformComponent>(projectile);
    assert(std::abs(spawned.position.x - moved.position.x) < 0.001f);
    assert(std::abs(spawned.position.y - moved.position.y) < 0.001f);

    runtime.shutdown();
    PhysicsSystem::clear();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_instantiated_enemy_recycled_id_starts_fresh() {
    std::cout << "[Test] InstantiatedEnemy_RecycledIdStartsFresh... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_instantiated_enemy_velocity_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/EnemySpawner.e2s", R"(
@behaviour
export class EnemySpawner extends Behaviour {
    constructor(@inject private prefabs: Prefabs) {}

    @expose
    enemyPrefab: Prefab;

    private spawned: Bool = false;

    override update(deltaTime: Float): Void {
        if (spawned) return;
        spawned = true;
        prefabs.instantiate(enemyPrefab, Vec2(300.0, 0.0));
    }
}
)");
    write_test_file(root / "Assets/Scripts/EnemyMover.e2s", R"(
@behaviour
export class EnemyMover extends Behaviour {
    constructor(
        @inject private entities: Entities,
        @component private body: RigidBody2D,
        @component private transform: Transform2D
    ) {}

    override on_start(): Void {
        body.velocity = Vec2(123.0, 0.0);
    }

    override update(deltaTime: Float): Void {
        let player: Entity = entities.findByGameplayTag("player");
        if (!entities.exists(player) || !entities.hasTransform(player)) {
            body.velocity = Vec2.zero();
            return;
        }
        let offset: Vec2 = entities.transform(player).position - transform.position;
        body.velocity = Vec2.normalized(offset) * 75.0;
    }
}
)");
    write_test_file(root / "Assets/Prefabs/Enemy.prefab", "{}");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* spawner_script = asset_database.find_by_path(root / "Assets/Scripts/EnemySpawner.e2s");
    const AssetRecord* enemy_script = asset_database.find_by_path(root / "Assets/Scripts/EnemyMover.e2s");
    const AssetRecord* prefab_record = asset_database.find_by_path(root / "Assets/Prefabs/Enemy.prefab");
    assert(spawner_script && enemy_script && prefab_record);

    EntityManager prefab_source;
    const EntityID prefab_root = prefab_source.create_entity();
    TransformComponent prefab_transform;
    prefab_transform.scale = {0.55f, 0.55f};
    prefab_transform.rotation = 12.0f;
    prefab_source.add_component<TransformComponent>(prefab_root, prefab_transform);
    prefab_source.add_component<GameplayTagsComponent>(prefab_root, GameplayTagsComponent{{"enemy.basic"}});
    RigidBodyComponent prefab_body;
    prefab_body.body_type = BodyType::Kinematic;
    prefab_body.gravity_scale = 0.0f;
    prefab_source.add_component<RigidBodyComponent>(prefab_root, prefab_body);
    ColliderComponent prefab_collider;
    ColliderShape prefab_shape;
    prefab_shape.type = ColliderShapeType::Circle;
    prefab_shape.radius = 25.0f;
    prefab_shape.is_trigger = true;
    prefab_collider.shapes.push_back(prefab_shape);
    prefab_source.add_component<ColliderComponent>(prefab_root, prefab_collider);
    ScriptComponent prefab_scripts;
    prefab_scripts.instances.front().enabled = true;
    prefab_scripts.instances.front().script_handle = enemy_script->handle;
    prefab_source.add_component<ScriptComponent>(prefab_root, prefab_scripts);
    const ComponentSerializationContext serialization_context{
        .asset_database = &asset_database
    };
    const std::string prefab_json = EntitySubtreeArchive::capture_to_json(
        prefab_source, prefab_root, serialization_context
    );
    assert(!prefab_json.empty());

    EntityManager entity_manager;
    EventDispatcher dispatcher;
    PhysicsSystem::clear();
    const EntityID player = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(player, TransformComponent{});
    entity_manager.add_component<GameplayTagsComponent>(player, GameplayTagsComponent{{"player"}});

    const EntityID spawner = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(spawner, TransformComponent{});
    ScriptComponent spawner_component;
    spawner_component.instances.front().enabled = true;
    spawner_component.instances.front().script_handle = spawner_script->handle;
    spawner_component.instances.front().public_fields = {
        {"enemyPrefab", ScriptFieldType::Asset, prefab_record->handle, {}, "Prefab"}
    };
    entity_manager.add_component<ScriptComponent>(spawner, spawner_component);

    EntityID enemy = 0;
    ScriptRuntime runtime;
    ScriptRuntimeServices services;
    services.instantiate_prefab = [&](AssetHandle handle, const glm::vec2& position) {
        assert(handle == prefab_record->handle);
        const SubtreeInstantiationResult result = EntitySubtreeArchive::instantiate_from_json(
            entity_manager, prefab_json, std::nullopt, 0, serialization_context
        );
        assert(result.success);
        enemy = result.root_entity;
        TransformComponent& spawned_transform = entity_manager.get_component<TransformComponent>(enemy);
        spawned_transform.position = {position.x, position.y, 0.0f};
        TransformSystem::update(entity_manager);
        PhysicsSystem::rebuild_body_hierarchy(entity_manager, enemy);
        for (const EntityID created : result.created_entities) runtime.queue_entity_start(created);
        return enemy;
    };

    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, &dispatcher, services);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.016f);
    assert(enemy != 0);
    assert(runtime.instance_count() == 2);
    const TransformComponent& spawned_transform = entity_manager.get_component<TransformComponent>(enemy);
    assert(std::abs(spawned_transform.scale.x - 0.55f) < 0.001f);
    assert(std::abs(spawned_transform.scale.y - 0.55f) < 0.001f);
    assert(std::abs(spawned_transform.rotation - 12.0f) < 0.001f);
    assert(std::abs(entity_manager.get_component<RigidBodyComponent>(enemy).velocity.x - 123.0f) < 0.001f);
    runtime.update(0.016f);

    const glm::vec2 velocity = entity_manager.get_component<RigidBodyComponent>(enemy).velocity;
    assert(std::abs(velocity.x + 75.0f) < 0.001f);
    assert(std::abs(velocity.y) < 0.001f);
    PhysicsSystem::update(entity_manager, dispatcher, 0.1f);
    assert(entity_manager.get_component<TransformComponent>(enemy).position.x < 300.0f);

    // Destroy the first runtime prefab and immediately reuse its numeric ID.
    // The replacement must receive a new behaviour object and run on_start();
    // otherwise the stale instance writes its normal chase velocity (+75).
    const EntityID first_enemy = enemy;
    entity_manager.destroy_entity(first_enemy);
    const EntityID reused_enemy = services.instantiate_prefab(prefab_record->handle, {-300.0f, 0.0f});
    assert(reused_enemy == first_enemy);
    runtime.update(0.016f);
    assert(runtime.instance_count() == 2);
    assert(std::abs(entity_manager.get_component<RigidBodyComponent>(reused_enemy).velocity.x - 123.0f) < 0.001f);

    runtime.shutdown();
    PhysicsSystem::clear();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_transform_write_moves_kinematic_body() {
    std::cout << "[Test] TransformWrite_MovesKinematicBody... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_transform_physics_sync_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/MoveKinematic.e2s", R"(
@behaviour
export class MoveKinematic extends Behaviour {
    constructor(@component private transform: Transform2D) {}

    override update(deltaTime: Float): Void {
        transform.position = Vec2(215.0, -84.0);
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* script_record = asset_database.find_by_path(root / "Assets/Scripts/MoveKinematic.e2s");
    assert(script_record);

    EntityManager entity_manager;
    EventDispatcher dispatcher;
    ScriptRuntime runtime;
    const EntityID entity = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(entity, TransformComponent{});
    RigidBodyComponent body;
    body.body_type = BodyType::Kinematic;
    body.gravity_scale = 0.0f;
    entity_manager.add_component<RigidBodyComponent>(entity, body);
    ColliderComponent collider;
    ColliderShape shape;
    shape.type = ColliderShapeType::Circle;
    shape.radius = 8.0f;
    collider.shapes.push_back(shape);
    entity_manager.add_component<ColliderComponent>(entity, collider);
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = script_record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script);

    PhysicsSystem::clear();
    TransformSystem::update(entity_manager);
    PhysicsSystem::update(entity_manager, dispatcher, 0.016f);
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, &dispatcher);
    assert(started.started == 1 && started.failed == 0);

    runtime.update(0.016f);
    PhysicsSystem::update(entity_manager, dispatcher, 0.016f);

    const TransformComponent& moved = entity_manager.get_component<TransformComponent>(entity);
    assert(std::abs(moved.position.x - 215.0f) < 0.001f);
    assert(std::abs(moved.position.y + 84.0f) < 0.001f);

    runtime.shutdown();
    PhysicsSystem::clear();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_behaviour_self_can_destroy_owner() {
    std::cout << "[Test] BehaviourSelf_DestroysOwner... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_behaviour_self_tests";
    std::error_code error;
    fs::remove_all(root, error);

    const std::string code = R"(
@behaviour
export class SelfDestruct extends Behaviour {
    override update(deltaTime: Float): Void {
        self().destroy();
    }
}
)";
    write_test_file(root / "Assets/scripts/SelfDestruct.e2s", code);

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* record = asset_database.find_by_path(root / "Assets/scripts/SelfDestruct.e2s");
    assert(record != nullptr);

    EntityManager entity_manager;
    ScriptRuntime runtime;
    const EntityID entity = entity_manager.create_entity();
    const EntityID child = entity_manager.create_entity();
    entity_manager.add_component<TransformComponent>(entity, TransformComponent{});
    entity_manager.add_component<TransformComponent>(child, TransformComponent{});
    assert(entity_manager.set_parent(child, entity).changed());
    ScriptComponent script;
    script.instances.front().enabled = true;
    script.instances.front().script_handle = record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script);

    ScriptRuntimeServices services;
    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, nullptr, services);
    assert(started.started == 1 && started.failed == 0);
    runtime.update(0.016f);
    assert(!entity_manager.is_alive(entity));
    assert(!entity_manager.is_alive(child));

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_button_click_dispatches_to_owning_behaviour() {
    std::cout << "[Test] UIButtonClick_DispatchesToOwningBehaviour... ";

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_button_script_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_test_file(root / "Assets/Scripts/Clickable.e2s", R"(
@behaviour
export class Clickable extends Behaviour {
    @expose
    clicks: Int = 0;

    override on_button_clicked(): Void {
        clicks = clicks + 1;
    }
}
)");

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));
    const AssetRecord* record = asset_database.find_by_path(root / "Assets/Scripts/Clickable.e2s");
    assert(record != nullptr);

    EntityManager entity_manager;
    EventDispatcher dispatcher;
    ScriptRuntime runtime;
    const EntityID first = entity_manager.create_entity();
    const EntityID second = entity_manager.create_entity();
    for (const EntityID entity : {first, second}) {
        entity_manager.add_component<UIButtonComponent>(entity, UIButtonComponent{});
        ScriptComponent script;
        script.instances.front().enabled = true;
        script.instances.front().script_handle = record->handle;
        entity_manager.add_component<ScriptComponent>(entity, script);
    }

    const ScriptRuntimeStartResult started = runtime.start(entity_manager, asset_database, &dispatcher);
    assert(started.started == 2 && started.failed == 0);
    dispatcher.publish(UIButtonClickedEvent{second});
    runtime.dispatch_events();

    const auto clicks_for = [&](EntityID entity) {
        const auto& fields = entity_manager.get_component<ScriptComponent>(entity).instances.front().public_fields;
        const auto found = std::find_if(fields.begin(), fields.end(), [](const ScriptPublicField& field) {
            return field.name == "clicks";
        });
        assert(found != fields.end());
        return std::get<double>(found->value);
    };
    assert(clicks_for(first) == 0.0);
    assert(clicks_for(second) == 1.0);

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
    test_particle_services_and_component();
    test_world_text_service();
    test_world_text_queue_is_bounded();
    test_collection_range_error_is_controlled();
    test_entity_transform_following_across_updates();
    test_member_initializer_keeps_component_field_reference();
    test_component_views_do_not_alias_between_entities();
    test_exposed_component_reference_targets_other_entity();
    test_coin_pickup_spawns_prefab_at_knight_position();
    test_update_spawns_prefab_at_moved_owner_position();
    test_instantiated_enemy_recycled_id_starts_fresh();
    test_transform_write_moves_kinematic_body();
    test_behaviour_self_can_destroy_owner();
    test_button_click_dispatches_to_owning_behaviour();
    test_async_scheduler_cancellation();
    test_game_session_transitions();
    test_e2s_script_inspection();
    std::cout << "=== All EngineScript Play Mode Tests Passed! ===\n";
    return 0;
}

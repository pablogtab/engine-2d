#include "SceneManager.hpp"
#include "CustomScene.hpp"
#include "SceneSerializer.hpp"
#include "../renderer/material/MaterialAsset.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/Log.hpp"
#include "../core/Time.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../enginescript/module/GameModule.hpp"
#include <algorithm>
#include <fstream>

namespace Engine {

namespace {
    const char* SCENE_STATE_PRIMARY = "build/.scene_state";
    const char* SCENE_STATE_FALLBACK = ".scene_state";

}

MaterialAssetRefreshResult SceneManager::refresh_material_assets(const AssetDatabase& database) {
    MaterialAssetRefreshResult result;
    const auto refresh = [this, &result, &database](const std::shared_ptr<Scene>& scene) {
        if (!scene) return;
        const auto refreshed = MaterialAsset::refresh_assigned(scene->get_entity_manager(), database);
        result.reloaded += refreshed.reloaded;
        result.unavailable += refreshed.unavailable;
        if (auto* custom = dynamic_cast<CustomScene*>(scene.get())) {
            static_cast<void>(custom->refresh_render_layer_effects(database));
        } else if (auto context = m_execution_contexts.find(scene.get()); context != m_execution_contexts.end()) {
            static_cast<void>(context->second->renderer.refresh_render_layer_effects(*scene, database));
        }
    };
    for (const auto& scene : m_scenes) refresh(scene);
    refresh(m_transient_scene);
    refresh(m_runtime_scene);
    return result;
}

std::size_t SceneManager::preview_material_asset(
    AssetHandle handle,
    std::shared_ptr<const Material> material
) {
    std::size_t count = 0;
    const auto preview = [&](const std::shared_ptr<Scene>& scene) {
        if (scene) count += MaterialAsset::preview_assigned(
            scene->get_entity_manager(), handle, material
        );
    };
    for (const auto& scene : m_scenes) preview(scene);
    preview(m_transient_scene);
    preview(m_runtime_scene);
    return count;
}

void SceneManager::save_state() {
    if (m_active_index < 0 || static_cast<size_t>(m_active_index) >= m_scenes.size()) return;

    std::ofstream out(SCENE_STATE_PRIMARY);
    if (!out.is_open()) {
        out.open(SCENE_STATE_FALLBACK);
    }
    if (out.is_open()) {
        out << m_scenes[m_active_index]->get_name() << "\n";
    }
}

void SceneManager::restore_state() {
    std::ifstream in(SCENE_STATE_PRIMARY);
    if (!in.is_open()) {
        in.open(SCENE_STATE_FALLBACK);
    }
    if (in.is_open()) {
        std::string name;
        if (std::getline(in, name) && !name.empty()) {
            set_active_scene(name);
        }
    }
}

void SceneManager::add_scene(std::shared_ptr<Scene> scene) {
    if (!scene) return;
    
    // Check if scene with same name already exists: replace it instead of duplicating!
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i] && m_scenes[i]->get_name() == scene->get_name()) {
            if (m_active_index == static_cast<int>(i)) {
                detach_scene(*m_scenes[i]);
            }
            discard_execution_context(m_scenes[i].get());
            m_scenes[i] = scene;
            if (m_active_index == static_cast<int>(i)) {
                attach_scene(*m_scenes[i]);
                apply_active_view();
            }
            return;
        }
    }

    m_scenes.push_back(scene);
    
    // Automatically activate first registered scene
    if (m_active_index < 0) {
        set_active_scene(0);
    }
}

void SceneManager::set_active_scene(size_t index) {
    if (index >= m_scenes.size()) return;
    if (m_active_index == static_cast<int>(index)) return; // Prevent redundant double attach!

    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        detach_scene(*m_scenes[m_active_index]);
    }

    m_active_index = static_cast<int>(index);
    attach_scene(*m_scenes[m_active_index]);
    apply_active_view();

    save_state();

    if (m_on_authoring_state_invalidated) m_on_authoring_state_invalidated();

    ENGINE_LOG_INFO("Switched to Scene [{}/{}]: '{}'", m_active_index + 1, m_scenes.size(), m_scenes[m_active_index]->get_name());
}

void SceneManager::set_active_scene(const std::string& name) {
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i]->get_name() == name) {
            set_active_scene(i);
            return;
        }
    }
}

void SceneManager::next_scene() {
    if (m_transient_scene) return; // blocked while in prefab stage
    if (m_scenes.empty()) return;
    size_t next_idx = (m_active_index + 1) % m_scenes.size();
    set_active_scene(next_idx);
}

void SceneManager::previous_scene() {
    if (m_transient_scene) return; // blocked while in prefab stage
    if (m_scenes.empty()) return;
    size_t prev_idx = (m_active_index - 1 + m_scenes.size()) % m_scenes.size();
    set_active_scene(prev_idx);
}

void SceneManager::start_play() {
    if (m_transient_scene) {
        ENGINE_LOG_WARN("[SCENE PLAY] Cannot enter Play mode while Prefab Stage is active.");
        return;
    }
    if (m_play_state == ScenePlayState::Pause) {
        resume_play();
        return;
    }

    if (!get_active_scene() || m_play_startup_phase != PlayStartupPhase::None) return;
    m_runtime_scene.reset();
    m_pending_runtime_scene.reset();
    m_requested_runtime_scene_path.clear();
    m_game_session.reset();
    m_play_startup_phase = PlayStartupPhase::Snapshot;
    m_play_load_progress = {true, 0.05f, "Capturing scene state", "Saving the authoring snapshot"};
}

void SceneManager::advance_play_startup() {
    Scene* active_scene = get_active_scene();
    if (!active_scene) {
        m_play_startup_phase = PlayStartupPhase::None;
        m_play_load_progress = {};
        return;
    }
    switch (m_play_startup_phase) {
        case PlayStartupPhase::Snapshot: {
            SceneSerializer serializer(active_scene, m_renderer);
            m_snapshot_json = serializer.serialize_to_string();
            m_play_startup_phase = PlayStartupPhase::PrepareRuntime;
            m_play_load_progress = {true, 0.35f, "Preparing runtime scene", "Attaching physics and game viewport"};
            break;
        }
        case PlayStartupPhase::PrepareRuntime: {
            detach_scene(*active_scene);
            m_play_state = ScenePlayState::Play;
            attach_scene(*active_scene);
            configure_scene_view(*active_scene, RenderViewKind::Game, m_game_view);
            m_play_state = ScenePlayState::Edit;
            m_play_startup_phase = PlayStartupPhase::StartScripts;
            std::size_t script_count = 0;
            const AssetDatabase& database = m_asset_database ? *m_asset_database : AssetDatabase::get();
            for (const AssetRecord& record : database.records()) {
                if (record.type == AssetType::Script && record.status == AssetStatus::Ready) ++script_count;
            }
            m_play_load_progress = {
                true,
                0.70f,
                "Compiling gameplay scripts",
                std::to_string(script_count) + " script" + (script_count == 1 ? "" : "s") + " into native module"
            };
            break;
        }
        case PlayStartupPhase::StartScripts:
            m_play_state = ScenePlayState::Play;
            set_play_time_scale(m_play_time_scale);
            start_scripts(*active_scene);
            m_play_startup_phase = PlayStartupPhase::None;
            m_play_load_progress = {};
            break;
        case PlayStartupPhase::None:
            break;
    }
}

void SceneManager::start_scripts(Scene& scene) {
    ScriptRuntimeServices services = m_script_services;
    services.request_scene_load = [this](const std::string& path, std::string& error) {
        return request_runtime_scene_load(path, error);
    };
    services.current_scene_name = [this]() {
        const Scene* scene = get_active_scene();
        return scene ? scene->get_name() : std::string{};
    };
    services.is_scene_loading = [this]() { return is_runtime_scene_loading(); };
    services.scene_load_error = [this]() { return runtime_scene_load_error(); };
    services.emit_particles = [this, &scene](AssetHandle handle, const glm::vec2& position, int count) {
        if (!is_playing() || dynamic_cast<CustomScene*>(&scene)) return false;
        return execution_context(scene).runtime.particle_world().emit_asset(
            handle,
            position,
            m_asset_database ? *m_asset_database : AssetDatabase::get(),
            count
        );
    };
    services.emit_particles_with_options = [this, &scene](
        AssetHandle handle,
        const glm::vec2& position,
        const EngineScript::ParticleEmitOptions& options
    ) {
        if (!is_playing() || dynamic_cast<CustomScene*>(&scene)) return false;
        ParticleEmissionOverrides overrides;
        overrides.count = options.count;
        if (options.overrideVelocity) overrides.velocity = options.velocity;
        if (options.overrideVelocityVariation) overrides.velocity_variation = options.velocityVariation;
        if (options.overrideColor) {
            const auto channel = [](float value) {
                return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            };
            overrides.color = SDL_Color{
                channel(options.color.x), channel(options.color.y),
                channel(options.color.z), channel(options.color.w)
            };
        }
        overrides.scale = options.scale;
        overrides.rotation = options.rotation;
        return execution_context(scene).runtime.particle_world().emit_asset(
            handle,
            position,
            m_asset_database ? *m_asset_database : AssetDatabase::get(),
            overrides
        );
    };
    services.play_particle_emitter = [this](EntityID entity) {
        return play_particle_emitter(entity);
    };
    services.stop_particle_emitter = [this](EntityID entity, bool clear_particles) {
        return stop_particle_emitter(entity, clear_particles);
    };
    services.burst_particle_emitter = [this](EntityID entity, int count) {
        return burst_particle_emitter(entity, count);
    };

    const ScriptRuntimeStartResult scripts = m_script_runtime.start(
        scene.get_entity_manager(),
        m_asset_database ? *m_asset_database : AssetDatabase::get(),
        &scene.get_event_dispatcher(),
        std::move(services)
    );
    ENGINE_LOG_INFO(
        "[SCENE PLAY] Started Play simulation: {} scripts running, {} failed",
        scripts.started,
        scripts.failed
    );
}

bool SceneManager::request_runtime_scene_load(const std::string& path, std::string& error) {
    if (m_play_state == ScenePlayState::Edit) {
        error = "Scenes can only be loaded during Play mode";
        return false;
    }
    if (!m_runtime_scene_loader) {
        error = "Runtime scene loader is unavailable";
        return false;
    }

    if (!m_game_session.request_scene(path, error)) return false;
    m_requested_runtime_scene_path = path;
    return true;
}

void SceneManager::advance_runtime_scene_load() {
    if (m_requested_runtime_scene_path.empty() || m_play_state == ScenePlayState::Edit) return;

    const std::string path = std::move(m_requested_runtime_scene_path);
    std::string error;
    std::shared_ptr<Scene> scene = m_runtime_scene_loader(path, error);
    if (!scene) {
        if (error.empty()) error = "Scene could not be loaded";
        m_game_session.complete_failed(error);
        if (Scene* active = get_active_scene()) {
            active->get_event_dispatcher().queue(GameSessionEvent{"SceneLoadFailed", path, error});
        }
        return;
    }
    m_pending_runtime_scene = std::move(scene);
}

void SceneManager::activate_pending_runtime_scene() {
    if (!m_pending_runtime_scene || m_play_state == ScenePlayState::Edit) return;

    m_script_runtime.shutdown();
    if (Scene* current = get_active_scene()) detach_scene(*current);

    m_runtime_scene = std::move(m_pending_runtime_scene);
    attach_scene(*m_runtime_scene);
    configure_scene_view(*m_runtime_scene, RenderViewKind::Game, m_game_view);
    start_scripts(*m_runtime_scene);
    m_game_session.complete_loaded();
    m_runtime_scene->get_event_dispatcher().queue(GameSessionEvent{"SceneLoaded", m_game_session.path()});
    ENGINE_LOG_INFO("[SCENE PLAY] Loaded runtime scene '{}'.", m_runtime_scene->get_name());
}

void SceneManager::pause_play() {
    if (m_play_state != ScenePlayState::Play) return;

    m_play_state = ScenePlayState::Pause;
    Time::set_time_scale(0.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Simulation Paused.");
}

void SceneManager::resume_play() {
    if (m_play_state != ScenePlayState::Pause) return;

    m_play_state = ScenePlayState::Play;
    Time::set_time_scale(m_play_time_scale);
    ENGINE_LOG_INFO("[SCENE PLAY] Simulation Resumed.");
}

void SceneManager::set_play_time_scale(float scale) {
    scale = std::clamp(scale, 0.0f, 2.0f);
    if (scale > 0.0f) m_play_time_scale = scale;
    Time::set_time_scale(scale);
    if (m_play_state != ScenePlayState::Edit) {
        m_play_state = scale == 0.0f ? ScenePlayState::Pause : ScenePlayState::Play;
    }
}

void SceneManager::step_play() {
    if (m_play_state != ScenePlayState::Pause || m_play_startup_phase != PlayStartupPhase::None) return;
    run_simulation_tick(1.0f / 60.0f);
    Time::set_time_scale(0.0f);
}

void SceneManager::stop_play() {
    if (m_play_state == ScenePlayState::Edit && m_play_startup_phase == PlayStartupPhase::None) return;
    if (m_play_state == ScenePlayState::Edit) {
        if (Scene* active_scene = get_active_scene()) {
            detach_scene(*active_scene);
            attach_scene(*active_scene);
            configure_scene_view(*active_scene, RenderViewKind::Editor, m_editor_view);
        }
        m_play_startup_phase = PlayStartupPhase::None;
        m_play_load_progress = {};
        return;
    }

    m_script_runtime.shutdown();
    m_pending_runtime_scene.reset();
    m_requested_runtime_scene_path.clear();
    m_game_session.cancel();
    const bool had_runtime_scene = m_runtime_scene != nullptr;
    if (had_runtime_scene) {
        detach_scene(*m_runtime_scene);
        discard_execution_context(m_runtime_scene.get());
        m_runtime_scene.reset();
    }
    m_play_state = ScenePlayState::Edit;

    Scene* active_scene = nullptr;
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        active_scene = m_scenes[m_active_index].get();
    }
    if (active_scene) {
        if (!had_runtime_scene) detach_scene(*active_scene);

        // Restore pristine authoring state from memory snapshot
        if (!m_snapshot_json.empty()) {
            SceneSerializer serializer(active_scene, m_renderer);
            serializer.deserialize_from_string(m_snapshot_json);
        }

        TransformSystem::update(active_scene->get_entity_manager());
        const auto& editor = m_editor_view;
        attach_scene(*active_scene);
        configure_scene_view(*active_scene, RenderViewKind::Editor, editor);
    }

    Time::set_time_scale(0.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Stopped Simulation: Authoring snapshot restored, switched back to Editor Camera.");
}

void SceneManager::update(float delta_time) {
    if (m_play_startup_phase != PlayStartupPhase::None) {
        advance_play_startup();
        return;
    }
    if (m_play_state != ScenePlayState::Play) {
        // Custom scenes are editor previews, not SceneRuntime simulation.
        // Preserve their established update contract in Edit/Pause mode.
        if (Scene* active = get_active_scene()) {
            if (auto* custom = dynamic_cast<CustomScene*>(active)) custom->on_update(delta_time);
        }
        return;
    }
    run_simulation_tick(delta_time);
}

void SceneManager::run_simulation_tick(float delta_time) {
    Scene* active = get_active_scene();
    if (active) {
        auto tick_scope = m_frame_profiler.scope("simulation.tick");
        if (!dynamic_cast<CustomScene*>(active)) {
            execution_context(*active).runtime.particle_world().begin_frame();
        }
        {
            auto scripts = m_frame_profiler.scope("scripts");
            static_cast<void>(m_script_runtime.reload_changed_scripts());
            m_script_runtime.update(delta_time);
        }
        if (auto* custom = dynamic_cast<CustomScene*>(active)) {
            auto custom_update = m_frame_profiler.scope("scene.update");
            custom->on_update(delta_time);
        } else {
            auto runtime = m_frame_profiler.scope("runtime.update");
            execution_context(*active).runtime.update(*active, delta_time);
        }
        {
            auto events = m_frame_profiler.scope("events");
            active->get_event_dispatcher().dispatch_queued();
            m_script_runtime.dispatch_events();
            activate_pending_runtime_scene();
            advance_runtime_scene_load();
        }
    }
}

void SceneManager::render(Renderer2D& renderer) {
    auto render_scope = m_frame_profiler.scope("render");
    renderer.clear_all_render_layer_effects();
    renderer.clear_all_entity_effect_profiles();
    Scene* active = get_active_scene();
    if (active) {
        if (auto* custom = dynamic_cast<CustomScene*>(active)) {
            custom->on_render(renderer);
        } else {
            auto& context = execution_context(*active);
            context.renderer.render(
                *active, context.runtime, context.editor_view, renderer, is_editing(),
                m_asset_database ? *m_asset_database : AssetDatabase::get()
            );
        }
    }
}

void SceneManager::configure_view(RenderViewKind view, const RenderViewConfiguration& configuration) {
    RenderViewConfiguration& metrics = view == RenderViewKind::Editor ? m_editor_view : m_game_view;
    RenderViewConfiguration normalized = configuration;
    normalized.output_width = std::max(normalized.output_width, 1);
    normalized.output_height = std::max(normalized.output_height, 1);
    normalized.reference_width = std::max(normalized.reference_width, 1);
    normalized.reference_height = std::max(normalized.reference_height, 1);
    if (metrics == normalized) return;
    metrics = normalized;

    const RenderViewKind active_view = is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    if (view == active_view) {
        Scene* active = get_active_scene();
        if (active) {
            configure_scene_view(*active, view, metrics);
        }
    }
}

void SceneManager::invalidate_render_resources() {
    for (const auto& scene : m_scenes) {
        if (!scene) continue;
        if (auto* custom = dynamic_cast<CustomScene*>(scene.get())) custom->on_render_resources_invalidated();
        else {
            auto& context = execution_context(*scene);
            context.renderer.invalidate_resources();
            context.runtime.particle_world().invalidate_render_resources();
        }
    }
    if (m_transient_scene) {
        if (auto* custom = dynamic_cast<CustomScene*>(m_transient_scene.get())) custom->on_render_resources_invalidated();
        else {
            auto& context = execution_context(*m_transient_scene);
            context.renderer.invalidate_resources();
            context.runtime.particle_world().invalidate_render_resources();
        }
    }
    if (m_runtime_scene) {
        if (auto* custom = dynamic_cast<CustomScene*>(m_runtime_scene.get())) custom->on_render_resources_invalidated();
        else {
            auto& context = execution_context(*m_runtime_scene);
            context.renderer.invalidate_resources();
            context.runtime.particle_world().invalidate_render_resources();
        }
    }
}

void SceneManager::apply_active_view() {
    Scene* active = get_active_scene();
    if (!active) return;
    const RenderViewKind view = is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    const RenderViewConfiguration& metrics = view == RenderViewKind::Editor ? m_editor_view : m_game_view;
    configure_scene_view(*active, view, metrics);
}


Scene* SceneManager::get_active_scene() const {
    if (m_transient_scene) return m_transient_scene.get();
    if (m_runtime_scene && m_play_state != ScenePlayState::Edit) return m_runtime_scene.get();
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        return m_scenes[m_active_index].get();
    }
    return nullptr;
}

std::shared_ptr<Scene> SceneManager::get_active_scene_shared_ptr() const {
    if (m_transient_scene) return m_transient_scene;
    if (m_runtime_scene && m_play_state != ScenePlayState::Edit) return m_runtime_scene;
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        return m_scenes[m_active_index];
    }
    return nullptr;
}

void SceneManager::enter_transient_scene(std::shared_ptr<Scene> stage) {
    if (!stage) return;
    if (m_transient_scene) {
        ENGINE_LOG_WARN("[SCENE] enter_transient_scene called while one is already active — ignoring.");
        return;
    }
    if (Scene* underlying = get_active_scene()) detach_scene(*underlying);
    m_transient_scene = std::move(stage);
    attach_scene(*m_transient_scene);
    configure_scene_view(*m_transient_scene, RenderViewKind::Editor, m_editor_view);
    ENGINE_LOG_INFO("[SCENE] Entered transient scene: '{}'", m_transient_scene->get_name());
}

void SceneManager::exit_transient_scene() {
    if (!m_transient_scene) return;
    detach_scene(*m_transient_scene);
    ENGINE_LOG_INFO("[SCENE] Exited transient scene: '{}'", m_transient_scene->get_name());
    discard_execution_context(m_transient_scene.get());
    m_transient_scene.reset();
    if (Scene* underlying = get_active_scene()) attach_scene(*underlying);
    apply_active_view();
}

SceneExecutionContext& SceneManager::execution_context(Scene& scene) {
    auto& context = m_execution_contexts[&scene];
    if (!context) context = std::make_unique<SceneExecutionContext>();
    return *context;
}

const SceneExecutionContext* SceneManager::find_execution_context(const Scene& scene) const {
    const auto found = m_execution_contexts.find(&scene);
    return found == m_execution_contexts.end() ? nullptr : found->second.get();
}

void SceneManager::discard_execution_context(const Scene* scene) {
    if (scene) m_execution_contexts.erase(scene);
}

void SceneManager::attach_scene(Scene& scene) {
    if (auto* custom = dynamic_cast<CustomScene*>(&scene)) {
        custom->set_is_editing(is_editing());
        custom->on_attach();
        return;
    }
    auto& context = execution_context(scene);
    context.runtime.attach(scene, m_renderer);
    context.attached = true;
}

void SceneManager::detach_scene(Scene& scene) {
    if (auto* custom = dynamic_cast<CustomScene*>(&scene)) {
        custom->on_detach();
        return;
    }
    auto& context = execution_context(scene);
    if (!context.attached) return;
    context.runtime.detach();
    context.renderer.detach();
    context.attached = false;
}

void SceneManager::configure_scene_view(
    Scene& scene, RenderViewKind view, const RenderViewConfiguration& configuration
) {
    if (auto* custom = dynamic_cast<CustomScene*>(&scene)) {
        custom->on_viewport_resize(view, configuration);
        return;
    }
    auto& context = execution_context(scene);
    if (view == RenderViewKind::Editor) context.editor_view.configure(configuration);
    else context.runtime.configure_game_view(configuration);
    context.renderer.configure_view(view, configuration);
}

Camera2D* SceneManager::get_render_camera(RenderViewKind view) {
    Scene* scene = get_active_scene();
    if (!scene) return nullptr;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) return custom->get_render_camera(view);
    auto& context = execution_context(*scene);
    return view == RenderViewKind::Editor ? &context.editor_view.camera() : &context.runtime.game_camera();
}

const Camera2D* SceneManager::get_render_camera(RenderViewKind view) const {
    const Scene* scene = get_active_scene();
    if (!scene) return nullptr;
    if (const auto* custom = dynamic_cast<const CustomScene*>(scene)) return custom->get_render_camera(view);
    const auto* context = find_execution_context(*scene);
    if (!context) return nullptr;
    return view == RenderViewKind::Editor ? &context->editor_view.camera() : &context->runtime.game_camera();
}

void SceneManager::set_render_filter(const IEntityRenderFilter* filter) {
    Scene* scene = get_active_scene();
    if (!scene) return;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) custom->set_render_filter(filter);
    else execution_context(*scene).editor_view.set_render_filter(filter);
}

SDL_Color SceneManager::get_clear_color() const {
    const Scene* scene = get_active_scene();
    if (!scene) return {24, 28, 36, 255};
    if (const auto* custom = dynamic_cast<const CustomScene*>(scene)) return custom->get_clear_color();
    const EntityManager& entities = scene->get_entity_manager();
    EntityID first = 0;
    EntityID primary = 0;
    for (const EntityID entity : entities.get_living_entities()) {
        if (!entities.has_component<CameraComponent>(entity) ||
            !entities.has_component<TransformComponent>(entity)) continue;
        if (first == 0) first = entity;
        if (entities.get_component<CameraComponent>(entity).is_primary) {
            primary = entity;
            break;
        }
    }
    const EntityID active = primary != 0 ? primary : first;
    return active != 0 ? entities.get_component<CameraComponent>(active).clear_color
                       : SDL_Color{24, 28, 36, 255};
}

PostProcessPipeline* SceneManager::get_post_process_pipeline() {
    Scene* scene = get_active_scene();
    if (!scene) return nullptr;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) return custom->get_post_process_pipeline();
    return execution_context(*scene).renderer.post_process_pipeline(
        *scene, m_asset_database ? *m_asset_database : AssetDatabase::get()
    );
}

PostProcessProfile* SceneManager::get_editable_post_process_profile() {
    Scene* scene = get_active_scene();
    if (!scene) return nullptr;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) return custom->get_editable_post_process_profile();
    return execution_context(*scene).renderer.editable_post_process_profile(
        *scene, m_asset_database ? *m_asset_database : AssetDatabase::get()
    );
}

AssetHandle SceneManager::get_post_process_profile_handle() const {
    Scene* scene = get_active_scene();
    if (!scene) return {};
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) return custom->get_post_process_profile_handle();
    const CameraComponent* camera = nullptr;
    auto& em = scene->get_entity_manager();
    em.for_each<CameraComponent>([&](EntityID, CameraComponent& cam) {
        if (!camera || cam.is_primary) {
            camera = &cam;
        }
    });
    return (camera && camera->post_process.enabled) ? camera->post_process.global_profile_handle : AssetHandle{};
}

bool SceneManager::apply_scene_settings(
    Scene& scene,
    const SceneSettings& settings,
    const AssetDatabase&
) {
    scene.settings() = settings;
    if (get_active_scene() == &scene) {
        PhysicsSystem::set_gravity(settings.gravity);
    }
    return true;
}

bool SceneManager::set_post_process_profile_handle(AssetHandle handle, const AssetDatabase& database) {
    Scene* scene = get_active_scene();
    if (!scene) return false;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) return custom->set_post_process_profile_handle(handle, database);
    return execution_context(*scene).renderer.set_post_process_profile(*scene, handle, database);
}

bool SceneManager::set_render_layer_effect_assignments(
    std::vector<RenderLayerEffectAssignment> assignments, const AssetDatabase& database
) {
    Scene* scene = get_active_scene();
    if (!scene) return false;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) {
        return custom->set_render_layer_effect_assignments(std::move(assignments), database);
    }
    return execution_context(*scene).renderer.set_render_layer_effects(
        *scene, std::move(assignments), database
    );
}

bool SceneManager::refresh_render_layer_effects(const AssetDatabase& database) {
    Scene* scene = get_active_scene();
    if (!scene) return false;
    if (auto* custom = dynamic_cast<CustomScene*>(scene)) return custom->refresh_render_layer_effects(database);
    return execution_context(*scene).renderer.refresh_render_layer_effects(*scene, database);
}

bool SceneManager::play_particle_emitter(EntityID entity) {
    Scene* scene = get_active_scene();
    if (!scene || !is_playing() || dynamic_cast<CustomScene*>(scene)) return false;
    return execution_context(*scene).runtime.particle_world().play_entity(
        entity, *scene, m_asset_database ? *m_asset_database : AssetDatabase::get()
    );
}

bool SceneManager::stop_particle_emitter(EntityID entity, bool clear_particles) {
    Scene* scene = get_active_scene();
    if (!scene || !is_playing() || dynamic_cast<CustomScene*>(scene)) return false;
    return execution_context(*scene).runtime.particle_world().stop_entity(
        entity, *scene, clear_particles
    );
}

bool SceneManager::burst_particle_emitter(EntityID entity, int count) {
    Scene* scene = get_active_scene();
    if (!scene || !is_playing() || dynamic_cast<CustomScene*>(scene)) return false;
    return execution_context(*scene).runtime.particle_world().burst_entity(
        entity,
        *scene,
        m_asset_database ? *m_asset_database : AssetDatabase::get(),
        count
    );
}

} // namespace Engine

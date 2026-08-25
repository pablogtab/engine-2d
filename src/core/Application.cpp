#include "Application.hpp"
#include "Log.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "FileSystem.hpp"
#include "SDLDisplayBackend.hpp"
#include "../audio/AudioEngine.hpp"
#if ENGINE_WITH_EDITOR
#include "../editor/EditorLayer.hpp"
#include "../editor/EditorSelection.hpp"
#include "../editor/commands/EditorCommandHistory.hpp"
#include "../editor/panels/ViewportPanel.hpp"
#include "../editor/panels/SceneHierarchyPanel.hpp"
#include "../editor/panels/InspectorPanel.hpp"
#include "../editor/panels/ConsolePanel.hpp"
#include "../editor/panels/StatsPanel.hpp"
#include "../editor/panels/SceneDiagnosticsPanel.hpp"
#include "../editor/panels/AssetBrowserPanel.hpp"
#include "../editor/panels/PostProcessTunerPanel.hpp"
#include "../editor/panels/ProjectSettingsPanel.hpp"
#include "../editor/panels/ShortcutsPreferencesPanel.hpp"
#include "../editor/panels/TilemapPalettePanel.hpp"
#include "../editor/panels/SpriteAnimationStudioPanel.hpp"
#include "../editor/panels/ParticleEffectEditorPanel.hpp"
#include "../editor/panels/AiAssistantPanel.hpp"
#endif
#include "../scene/Scene.hpp"
#include "../scene/SceneSerializer.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../renderer/material/SDLGPUMaterialBackend.hpp"
#include "../renderer/material/MaterialAsset.hpp"
#include <algorithm>
#include <filesystem>

namespace Engine {

namespace {

SafeAreaInsets map_safe_area_to_reference(
    const DisplayState& state,
    const ProjectDisplaySettings& settings
) {
    const glm::vec2 destination{
        static_cast<float>(std::max(state.pixel_width, 1)),
        static_cast<float>(std::max(state.pixel_height, 1))
    };
    const glm::vec2 source{
        static_cast<float>(std::max(settings.reference_width, 1)),
        static_cast<float>(std::max(settings.reference_height, 1))
    };
    const PresentationMode mapping_mode = settings.presentation == PresentationMode::Responsive
        ? PresentationMode::Stretch : settings.presentation;
    const PresentationMapping mapping = calculate_presentation(source, destination, mapping_mode);
    const glm::vec2 safe_min{state.safe_area.left, state.safe_area.top};
    const glm::vec2 safe_max{
        destination.x - state.safe_area.right,
        destination.y - state.safe_area.bottom
    };
    const glm::vec2 display_size = glm::max(mapping.display_size, glm::vec2(1.0f));
    const glm::vec2 uv_span = mapping.uv_max - mapping.uv_min;
    const auto to_source = [&](const glm::vec2& point) {
        const glm::vec2 normalized = (point - mapping.display_offset) / display_size;
        return (mapping.uv_min + normalized * uv_span) * source;
    };
    const glm::vec2 mapped_min = glm::clamp(to_source(safe_min), glm::vec2(0.0f), source);
    const glm::vec2 mapped_max = glm::clamp(to_source(safe_max), mapped_min, source);
    return {
        mapped_min.x,
        mapped_min.y,
        source.x - mapped_max.x,
        source.y - mapped_max.y
    };
}

} // namespace

Application::Application(ProjectContext project, const std::string& name, int width, int height, bool start_with_editor)
    : m_project(std::move(project)) {
    Log::init((m_project.library_directory() / "engine.log").string());
    ENGINE_LOG_INFO("Initializing Engine Application: '{}' ({}x{}) | Project: '{}'", name, width, height, m_project.root().string());
    FileSystem::init("Engine2D", m_project.descriptor().name, true, m_project.root());
    AssetDatabase::get().set_project_root(m_project.root());
    m_project_settings.load((m_project.settings_directory() / "Project.json").string());
    m_dynamic_resolution.configure(m_project_settings.display().render_resolution);
    [[maybe_unused]] const AssetScanResult asset_scan = AssetDatabase::get().scan("Assets");

    Input::init();
    Time::reset();
    AudioEngine::init();

    // 1. Create Window & SDL Renderer
    const auto& project_display = m_project_settings.display();
    SDL_SetHint(
        SDL_HINT_ORIENTATIONS,
        orientation_policy_sdl_hint(project_display.orientation_policy)
    );
    const int initial_width = start_with_editor ? width : project_display.default_window_width;
    const int initial_height = start_with_editor ? height : project_display.default_window_height;
    m_window = std::make_unique<Window>(name, initial_width, initial_height, start_with_editor);
    m_display_manager = std::make_unique<DisplayManager>(std::make_unique<SDLDisplayBackend>(
        m_window->get_sdl_window(),
        m_window->get_sdl_renderer()
    ));
    m_renderer = std::make_unique<Renderer2D>(m_window->get_sdl_renderer());
    m_renderer->set_material_backend(std::make_shared<SDLGPUMaterialBackend>(
        m_window->get_sdl_renderer()
    ));
    m_renderer->set_thread_pool(&m_thread_pool);
    m_runtime_asset_cache = std::make_unique<RuntimeAssetCache>();

    if (!start_with_editor) {
        DisplaySettings defaults;
        defaults.window_mode = project_display.default_window_mode;
        defaults.window_width = project_display.default_window_width;
        defaults.window_height = project_display.default_window_height;
        defaults.refresh_rate = project_display.default_refresh_rate;
        defaults.vsync = project_display.default_vsync;
        const DisplayApplyResult result = m_display_manager->apply(defaults);
        if (!result.success) {
            ENGINE_LOG_WARN("[DISPLAY] Could not apply project defaults: {}", result.message);
        }
    }

    m_display_manager->set_apply_enabled(!start_with_editor);
    m_scene_manager.set_renderer(m_window->get_sdl_renderer());
    m_scene_manager.set_asset_database(&AssetDatabase::get());
    ScriptRuntimeServices script_services;
    script_services.hot_reload_enabled = start_with_editor;
    script_services.preload_asset = [this](AssetHandle handle, std::string& error) {
        return m_runtime_asset_cache && m_runtime_asset_cache->preload(
            AssetDatabase::get(), handle, m_window->get_sdl_renderer(), error
        );
    };
    if (!start_with_editor) {
#if defined(_WIN32)
        constexpr const char* native_module_name = "EngineScript_1.dll";
#elif defined(__APPLE__)
        constexpr const char* native_module_name = "libEngineScript_1.dylib";
#else
        constexpr const char* native_module_name = "libEngineScript_1.so";
#endif
        const std::filesystem::path native_module = m_project.resolve(
            std::filesystem::path("Library/EngineScript/Generated/1") / native_module_name
        );
        if (std::filesystem::is_regular_file(native_module)) {
            script_services.native_module_path = native_module.string();
        }
    }
    script_services.play_sound = [](const std::string& path, float volume, float pitch, std::string&) {
        AudioEngine::play_sound(path, volume, pitch);
        return true;
    };
    script_services.play_music = [](const std::string& path, float volume, bool loop, std::string&) {
        AudioEngine::play_music(path, volume, loop);
        return true;
    };
    script_services.stop_music = []() { AudioEngine::stop_music(); };
    script_services.pause_music = []() { AudioEngine::pause_music(); };
    script_services.resume_music = []() { AudioEngine::resume_music(); };
    script_services.set_master_volume = [](float volume) { AudioEngine::set_master_volume(volume); };
    script_services.set_music_volume = [](float volume) { AudioEngine::set_music_volume(volume); };
    script_services.set_sfx_volume = [](float volume) { AudioEngine::set_sfx_volume(volume); };
    script_services.get_master_volume = []() { return AudioEngine::get_master_volume(); };
    script_services.get_music_volume = []() { return AudioEngine::get_music_volume(); };
    script_services.get_sfx_volume = []() { return AudioEngine::get_sfx_volume(); };
    script_services.query_display = [this]() {
        const DisplayState& state = m_display_manager->current_state();
        const ProjectDisplaySettings& project = m_project_settings.display();
        return ScriptDisplayInfo{
            .pixel_width = state.pixel_width,
            .pixel_height = state.pixel_height,
            .dpi_scale = state.dpi_scale,
            .safe_area = state.safe_area,
            .window_mode = state.settings.window_mode,
            .vsync = state.settings.vsync,
            .reference_width = project.reference_width,
            .reference_height = project.reference_height
        };
    };
    script_services.apply_display = [this](const ScriptDisplayRequest& request, std::string& error) {
        DisplaySettings settings = m_display_manager->current_state().settings;
        if (request.window_mode) settings.window_mode = *request.window_mode;
        if (request.vsync) settings.vsync = *request.vsync;
        const DisplayApplyResult result = m_display_manager->apply(settings);
        error = result.message;
        return result.success;
    };
    m_scene_manager.set_script_runtime_services(std::move(script_services));
    m_scene_manager.set_runtime_scene_loader([this](const std::string& path, std::string& error) {
        const std::filesystem::path absolute_path = m_project.resolve(path);
        auto scene = std::make_shared<Scene>(absolute_path.stem().string());
        SceneSerializer serializer(scene.get(), m_window->get_sdl_renderer());
        if (!serializer.deserialize(absolute_path.string())) {
            error = "Could not deserialize scene asset: " + path;
            return std::shared_ptr<Scene>{};
        }
        return std::shared_ptr<Scene>{std::move(scene)};
    });
    AssetDatabase::get().set_on_assets_changed([this]() {
        const auto refreshed = m_scene_manager.refresh_material_assets(AssetDatabase::get());
        if (m_renderer && m_renderer->get_material_backend()) m_renderer->get_material_backend()->discard_unused();
        if (refreshed.reloaded || refreshed.unavailable) {
            ENGINE_LOG_INFO("[ASSETS] Refreshed {} materials, {} unavailable", refreshed.reloaded, refreshed.unavailable);
        }
    });

    m_window->set_close_request_callback([this]() {
#if ENGINE_WITH_EDITOR
        if (m_editor_layer && m_editor_layer->is_visible()) {
            return m_editor_layer->request_exit();
        }
#endif
        return true;
    });

#if ENGINE_WITH_EDITOR
    if (start_with_editor) {
        m_editor_layer = std::make_unique<EditorLayer>();
        m_editor_layer->init(m_window->get_sdl_window(), m_window->get_sdl_renderer(), m_project.root());
        m_editor_layer->set_window_wrapper(m_window.get());
        m_editor_layer->set_visible(true);
        m_editor_layer->set_scene_manager(&m_scene_manager);
        m_editor_layer->set_project_hub_request_callback([this]() {
            m_open_project_hub_after_exit = true;
            m_running = false;
        });

        auto selection = std::make_shared<EditorSelection>();
        auto command_history = std::make_shared<EditorCommandHistory>();

        m_editor_layer->set_command_history(command_history);
        m_scene_manager.set_on_authoring_state_invalidated([this]() {
            if (m_editor_layer) {
                auto history = m_editor_layer->get_command_history();
                if (history) {
                    history->clear_history_preserving_dirty_state();
                }
            }
        });

    auto viewport_panel = std::make_shared<ViewportPanel>(
        m_window->get_sdl_renderer(),
        &m_scene_manager,
        selection,
        command_history,
        &m_project_settings
    );
    viewport_panel->set_engine_renderer(m_renderer.get());
    const auto& game_view = m_project_settings.display();
    const SafeAreaInsets editor_safe_area = map_safe_area_to_reference(
        m_display_manager->current_state(), game_view
    );
    viewport_panel->set_safe_area_preview(editor_safe_area);
    m_scene_manager.configure_view(RenderViewKind::Game, {
        game_view.reference_width, game_view.reference_height,
        game_view.reference_width, game_view.reference_height,
        editor_safe_area
    });
    auto hierarchy_panel = std::make_shared<SceneHierarchyPanel>(&m_scene_manager, m_window->get_sdl_renderer(), selection, command_history);
    auto inspector_panel = std::make_shared<InspectorPanel>(
        &m_scene_manager,
        selection,
        m_window->get_sdl_renderer(),
        &get_asset_manager()
    );
    inspector_panel->set_command_history(command_history);
    auto console_panel = std::make_shared<ConsolePanel>();
    auto stats_panel = std::make_shared<StatsPanel>(
        &m_scene_manager.frame_profiler(), m_project.root().string()
    );
    stats_panel->is_open = false; // Closed by default at startup (accessible via Panels menu)
    auto diagnostics_panel = std::make_shared<SceneDiagnosticsPanel>(
        &m_scene_manager, &get_asset_database(), selection
    );
    diagnostics_panel->set_command_history(command_history);
    diagnostics_panel->is_open = false;
    auto asset_browser = std::make_shared<AssetBrowserPanel>(&get_asset_database(), m_project.assets_directory().string());
    asset_browser->set_renderer(m_window->get_sdl_renderer());
    asset_browser->set_material_backend(m_renderer->get_material_backend());
    asset_browser->set_command_history(command_history);
    asset_browser->set_on_material_preview([this](
        AssetHandle handle, std::shared_ptr<const Material> material
    ) {
        static_cast<void>(m_scene_manager.preview_material_asset(handle, std::move(material)));
    });
    asset_browser->set_on_material_preview_reset([this]() {
        static_cast<void>(m_scene_manager.refresh_material_assets(AssetDatabase::get()));
    });
    inspector_panel->set_material_asset_actions(
        [asset_browser]() { return asset_browser->create_material_asset(); },
        [asset_browser](AssetHandle handle) {
            return asset_browser->duplicate_material_asset(handle);
        },
        [asset_browser](AssetHandle handle) { asset_browser->open_material_asset(handle); },
        [asset_browser](AssetHandle handle) {
            return asset_browser->preview_material_asset(handle);
        }
    );
    diagnostics_panel->set_on_reveal_asset([asset_browser](const std::string& asset_path) {
        asset_browser->reveal_asset_path(asset_path);
    });
    auto postprocess_panel = std::make_shared<PostProcessTunerPanel>(&m_scene_manager, command_history);
    postprocess_panel->is_open = false; // Closed by default at startup (accessible via View / Windows menu)


    auto project_command_history = std::make_shared<EditorCommandHistory>();
    auto project_settings_panel = std::make_shared<ProjectSettingsPanel>(&m_project_settings, &m_scene_manager, project_command_history);
    auto shortcuts_panel = std::make_shared<ShortcutsPreferencesPanel>();
    shortcuts_panel->is_open = false; // Closed by default until opened from Edit menu
    auto palette_panel = std::make_shared<TilemapPalettePanel>(m_window->get_sdl_renderer(), &m_scene_manager, selection);
    auto ai_assistant_panel = std::make_shared<AiAssistantPanel>(
        &m_scene_manager,
        selection,
        ComponentSerializationContext{
            .renderer = m_window->get_sdl_renderer(),
            .asset_manager = &get_asset_manager(),
            .asset_database = &get_asset_database()
        }
    );
    ai_assistant_panel->set_viewport_panel(viewport_panel);

    asset_browser->set_on_load_scene_callback([this](const std::string& scene_path) {
        if (m_editor_layer) {
            m_editor_layer->load_scene(scene_path);
        }
    });

    asset_browser->set_on_open_prefab_callback([this](const std::string& prefab_path) {
        if (m_editor_layer) {
            m_editor_layer->request_destructive_action({
                .type = PendingActionType::LoadScene,
                .target = prefab_path,
                .action = [this, prefab_path]() {
                    m_editor_layer->open_prefab_stage(prefab_path);
                }
            });
        }
    });

    viewport_panel->set_on_load_scene_callback([this](const std::string& scene_path) {
        if (m_editor_layer) {
            m_editor_layer->load_scene(scene_path);
        }
    });
    viewport_panel->set_on_activate_scene_callback([this](std::size_t index) {
        if (m_editor_layer) m_editor_layer->activate_scene(index);
    });
    viewport_panel->set_is_scene_dirty_callback([this](const Scene* scene) {
        return m_editor_layer && m_editor_layer->is_scene_dirty(scene);
    });
    viewport_panel->set_is_scene_tab_open_callback([this](const Scene* scene) {
        return m_editor_layer && m_editor_layer->is_scene_tab_open(scene);
    });

    viewport_panel->set_prefab_stage(m_editor_layer->get_prefab_stage());
    viewport_panel->set_on_close_prefab_stage_callback([this]() {
        if (m_editor_layer) {
            m_editor_layer->request_destructive_action({
                .type = PendingActionType::LoadScene,
                .action = []() {}
            });
        }
    });
    viewport_panel->set_on_save_prefab_callback([this]() {
        if (m_editor_layer) {
            m_editor_layer->save_active_scene();
        }
    });

    hierarchy_panel->set_prefab_stage(m_editor_layer->get_prefab_stage());
    inspector_panel->set_prefab_stage(m_editor_layer->get_prefab_stage());
    inspector_panel->set_on_open_prefab_callback([this](const std::string& prefab_path) {
        if (m_editor_layer) {
            m_editor_layer->request_destructive_action({
                .type = PendingActionType::LoadScene,
                .target = prefab_path,
                .action = [this, prefab_path]() {
                    m_editor_layer->open_prefab_stage(prefab_path);
                }
            });
        }
    });


    auto anim_studio_panel = std::make_shared<SpriteAnimationStudioPanel>(&m_scene_manager, selection);
    anim_studio_panel->set_command_history(command_history);
    inspector_panel->set_animation_studio(anim_studio_panel.get());

    auto particle_editor_panel = std::make_shared<ParticleEffectEditorPanel>(
        m_window->get_sdl_renderer(), command_history
    );
    particle_editor_panel->is_open = false;
    asset_browser->set_on_open_particle_effect([particle_editor_panel](AssetHandle handle) {
        particle_editor_panel->open_asset(handle);
        particle_editor_panel->is_open = true;
    });

    viewport_panel->set_palette_panel(palette_panel.get());
    m_editor_layer->set_viewport_panel(viewport_panel.get());
    m_editor_layer->set_selection(selection);
    m_editor_layer->register_panel(viewport_panel);
    m_editor_layer->register_panel(hierarchy_panel);
    m_editor_layer->register_panel(inspector_panel);
    m_editor_layer->register_panel(console_panel);
    m_editor_layer->register_panel(asset_browser);
    m_editor_layer->register_panel(stats_panel);
    m_editor_layer->register_panel(diagnostics_panel);
    m_editor_layer->register_panel(postprocess_panel);
    m_editor_layer->register_panel(particle_editor_panel);
    m_editor_layer->register_panel(project_settings_panel);
    m_editor_layer->register_panel(shortcuts_panel);
    m_editor_layer->register_panel(palette_panel);
    m_editor_layer->register_panel(anim_studio_panel);
    m_editor_layer->register_panel(ai_assistant_panel);

        // Connect Log stream to in-game ConsolePanel
        Log::set_callback([console_panel](const std::string& timestamp, const std::string& level, const std::string& message) {
            console_panel->add_log(timestamp, level, message);
        });
    }
#else
    (void)start_with_editor;
    ENGINE_LOG_INFO("[RUNTIME] Starting without editor services");
#endif

    // Event routing: Pass events through EditorLayer and Input Manager (isolated via Viewport focus)
    m_window->set_event_callback([this](const SDL_Event& event) {
#if ENGINE_WITH_EDITOR
        if (this->m_editor_layer) {
            this->m_editor_layer->process_event(event);
        }
#endif
        Input::process_event(event);
    });
    m_window->set_lifecycle_callback([this](ApplicationLifecycleEvent event) {
        this->handle_lifecycle_event(event);
    });

    m_display_manager->set_change_callback([this](const DisplayState& state) {
        this->on_resize(state.pixel_width, state.pixel_height);
        this->apply_display_mode(
#if ENGINE_WITH_EDITOR
            this->m_editor_layer && this->m_editor_layer->is_visible()
#else
            false
#endif
        );
        this->on_display_changed(state);
    });
    m_window->set_display_change_callback([this]() {
        this->handle_render_surface_changed();
    });

    // Request focus only after the editor and all SDL event consumers are
    // initialized. Raising the window during early window construction is
    // racy on macOS when dev.sh has just replaced the previous process.
    m_window->request_input_focus();
}

Application::~Application() {
    // GPU-backed assets must be released while the SDL renderer still exists.
    AssetManager::get().clear();
}

void Application::apply_display_mode(bool editor_visible) {
    m_display_manager->set_apply_enabled(!editor_visible);
    const auto& game_view = m_project_settings.display();
#if ENGINE_WITH_EDITOR
    if (editor_visible) {
        m_window->disable_logical_presentation();
        auto* viewport = m_editor_layer ? m_editor_layer->get_viewport_panel() : nullptr;
        if (viewport) {
            const SafeAreaInsets preview_safe_area = map_safe_area_to_reference(
                m_display_manager->current_state(), game_view
            );
            viewport->set_safe_area_preview(preview_safe_area);
            const int width = std::max(viewport->get_render_width(), 1);
            const int height = std::max(viewport->get_render_height(), 1);
            m_scene_manager.configure_view(RenderViewKind::Editor, {
                width, height, width, height,
                preview_safe_area,
                viewport->show_safe_area_overlay()
            });
        }
        return;
    }
#else
    (void)editor_visible;
#endif
    const RenderViewKind view = m_scene_manager.is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    m_window->set_logical_presentation(game_view.reference_width, game_view.reference_height, game_view.presentation);
    if (game_view.presentation == PresentationMode::Responsive) {
        m_scene_manager.configure_view(view, {
            m_display_manager->current_state().pixel_width,
            m_display_manager->current_state().pixel_height,
            game_view.reference_width, game_view.reference_height,
            m_display_manager->current_state().safe_area
        });
    } else {
        m_scene_manager.configure_view(view, {
            game_view.reference_width, game_view.reference_height,
            game_view.reference_width, game_view.reference_height,
            map_safe_area_to_reference(m_display_manager->current_state(), game_view)
        });
    }
}

bool Application::editor_visible() const {
#if ENGINE_WITH_EDITOR
    return m_editor_layer && m_editor_layer->is_visible();
#else
    return false;
#endif
}

void Application::trim_render_resources() {
    if (m_renderer) m_renderer->trim_transient_resources();
    m_scene_manager.invalidate_render_resources();
#if ENGINE_WITH_EDITOR
    if (m_editor_layer) m_editor_layer->trim_render_resources();
#endif
}

void Application::invalidate_render_surface() {
    if (m_renderer) m_renderer->invalidate_render_surface();
    m_scene_manager.invalidate_render_resources();
#if ENGINE_WITH_EDITOR
    if (m_editor_layer) m_editor_layer->invalidate_render_resources();
#endif
}

void Application::restore_render_surface() {
    if (m_renderer) m_renderer->restore_render_surface();
#if ENGINE_WITH_EDITOR
    if (m_editor_layer) m_editor_layer->restore_render_resources();
#endif
}

void Application::handle_render_surface_changed() {
    if (m_renderer && m_renderer->is_render_surface_available()) {
        m_renderer->trim_transient_resources();
        m_scene_manager.invalidate_render_resources();
    }
    if (m_display_manager) m_display_manager->refresh_from_system();
}

void Application::handle_lifecycle_event(ApplicationLifecycleEvent event) {
    const ApplicationLifecycleActions actions = m_lifecycle.handle(event);
    if (actions.trim_memory) {
        ENGINE_LOG_INFO("[LIFECYCLE] Low-memory notification: releasing transient render resources");
        trim_render_resources();
        on_low_memory();
    }
    if (actions.suspend_runtime) {
        ENGINE_LOG_INFO("[LIFECYCLE] Suspending runtime");
        Input::set_game_input_enabled(false);
        Input::reset_states();
        Time::suspend();
        AudioEngine::suspend();
        on_application_suspended();
    }
    if (actions.invalidate_render_surface) invalidate_render_surface();
    if (actions.restore_render_surface) restore_render_surface();
    if (actions.refresh_display && m_display_manager) {
        const DisplayState previous = m_display_manager->current_state();
        m_display_manager->refresh_from_system();
        if (m_display_manager->current_state() == previous) {
            on_resize(previous.pixel_width, previous.pixel_height);
            apply_display_mode(editor_visible());
        }
    }
    if (actions.resume_runtime) {
        Input::reset_states();
        AudioEngine::resume();
        Time::resume();
        ENGINE_LOG_INFO("[LIFECYCLE] Runtime resumed");
        on_application_resumed();
    }
    if (actions.reset_frame_clock) m_reset_frame_clock_requested = true;
    if (actions.request_termination) {
        ENGINE_LOG_INFO("[LIFECYCLE] Termination requested by the operating system");
        on_terminating();
        m_running = false;
    }
}

void Application::run() {
    on_init();

    // Trigger initial on_resize
    const DisplayState& display_state = m_display_manager->current_state();
    int pixel_w = display_state.pixel_width;
    int pixel_h = display_state.pixel_height;
    on_resize(pixel_w, pixel_h);
    apply_display_mode(
#if ENGINE_WITH_EDITOR
        m_editor_layer && m_editor_layer->is_visible()
#else
        false
#endif
    );

    uint64_t last_counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();
    bool last_editor_visible = false;
#if ENGINE_WITH_EDITOR
    last_editor_visible = m_editor_layer ? m_editor_layer->is_visible() : false;
#endif

    while (m_running && !m_window->is_should_close()) {
        uint64_t current_counter = SDL_GetPerformanceCounter();
        float raw_delta_time = static_cast<float>(current_counter - last_counter) / static_cast<float>(frequency);
        last_counter = current_counter;

        // 0. Update global engine time clock (handles time_scale, real_dt, and clamping)
        Time::update(raw_delta_time);
        float dt = Time::get_delta_time();

        // 1. Reset frame input delta states, then process window & input events
        Input::update();
        m_window->poll_events();
        if (m_reset_frame_clock_requested) {
            last_counter = SDL_GetPerformanceCounter();
            Time::reset_frame_delta();
            raw_delta_time = 0.0f;
            dt = 0.0f;
            m_reset_frame_clock_requested = false;
        }
        if (!m_lifecycle.should_run_frames() ||
            !m_renderer || !m_renderer->is_render_surface_available()) {
            SDL_Delay(16);
            continue;
        }

        // Handle Editor visibility change (F1 key toggle)
#if ENGINE_WITH_EDITOR
        if (m_editor_layer) {
            bool is_vis = m_editor_layer->is_visible();
            if (is_vis != last_editor_visible) {
                last_editor_visible = is_vis;
                apply_display_mode(is_vis);
            }
        }
#endif

        // 2. Begin Editor Frame (DockSpace & MenuBar)
#if ENGINE_WITH_EDITOR
        if (m_editor_layer) {
            m_editor_layer->begin_frame();
            m_editor_layer->render_dockspace();
        }
#endif

        // Handle TAB / Shift+TAB for cycling demo scenes when in game mode or not typing in an ImGui input field
        bool is_imgui_typing = false;
#if ENGINE_WITH_EDITOR
        is_imgui_typing = m_editor_layer && m_editor_layer->is_visible() && m_editor_layer->want_text_input();
#endif
        if (!is_imgui_typing && Input::is_key_just_pressed(SDL_SCANCODE_TAB)) {
            if (Input::is_key_pressed(SDL_SCANCODE_LSHIFT) || Input::is_key_pressed(SDL_SCANCODE_RSHIFT)) {
                m_scene_manager.previous_scene();
            } else {
                m_scene_manager.next_scene();
            }
        }

        // Configure Input isolation based on Viewport focus/hover state
#if ENGINE_WITH_EDITOR
        if (m_editor_layer && m_editor_layer->is_visible()) {
            auto* vp = m_editor_layer->get_viewport_panel();
            if (vp) {
                bool can_play = (vp->get_play_state() == EditorPlayState::Play);
                bool viewport_focused = vp->is_viewport_focused();
                bool viewport_hovered = vp->is_viewport_hovered();
                bool imgui_typing = m_editor_layer->want_text_input();

                // Game receives input when over/focused on Viewport and not actively typing in an ImGui text box
                bool allow_game_input = can_play && (viewport_focused || viewport_hovered) && !imgui_typing;
                Input::set_game_input_enabled(allow_game_input);
                Input::set_custom_mouse_position(vp->get_viewport_mouse_pos());
            }
        } else {
            Input::set_game_input_enabled(true);
        }
#else
        Input::set_game_input_enabled(true);
#endif

        // 3. Update Application logic & active Scene (scaled delta time)
        m_scene_manager.frame_profiler().begin_frame();
        bool is_playing = true;
        bool game_view_active = true;
#if ENGINE_WITH_EDITOR
        if (m_editor_layer && m_editor_layer->is_visible()) {
            auto* vp = m_editor_layer->get_viewport_panel();
            if (vp) {
                const bool play_starting = m_scene_manager.play_load_progress().active;
                is_playing = (vp->get_play_state() == EditorPlayState::Play) || play_starting;
                game_view_active = vp->get_play_state() != EditorPlayState::Edit || play_starting;
            }
        }
#endif

        on_update(dt);
        if (is_playing) {
            m_scene_manager.update(dt);
        } else {
            // In Edit / Paused mode: update transforms only for live inspector preview, but ZERO physics!
            auto* active_scene = m_scene_manager.get_active_scene();
            if (active_scene) {
                TransformSystem::update(active_scene->get_entity_manager());
            }
        }

        m_dynamic_resolution.configure(m_project_settings.display().render_resolution);
        if (game_view_active) {
            const float scale = m_dynamic_resolution.update(raw_delta_time);
            const auto& resolution = m_dynamic_resolution.settings();
            const float measured_fps = resolution.mode == RenderScaleMode::Dynamic
                ? m_dynamic_resolution.smoothed_fps()
                : (raw_delta_time > 0.0f ? 1.0f / raw_delta_time : 0.0f);
            m_renderer->set_render_resolution(
                scale,
                resolution.mode,
                resolution.mode == RenderScaleMode::Dynamic ? resolution.dynamic_target_fps : 0.0f,
                measured_fps
            );
        } else {
            m_renderer->set_render_resolution(1.0f, RenderScaleMode::Fixed);
        }

        // 4. Render Game Scene into Viewport Render Target (if editor is active) or to window
#if ENGINE_WITH_EDITOR
        ViewportPanel* vp = m_editor_layer ? m_editor_layer->get_viewport_panel() : nullptr;
        if (vp && m_editor_layer->is_visible()) {
            const SDL_Color scene_clear_color = m_scene_manager.get_clear_color();
            m_renderer->set_scene_clear_color(scene_clear_color);
            vp->begin_render();
            m_renderer->set_custom_render_target(vp->get_viewport_texture());
            on_render();
            m_scene_manager.render(*m_renderer);
            vp->end_render();
            m_renderer->set_custom_render_target(nullptr);

            // Clear main backbuffer with dark editor background
            m_window->clear(23, 26, 33, 255);
        } else {
            m_renderer->set_custom_render_target(nullptr);
            const SDL_Color scene_clear_color = m_scene_manager.get_clear_color();
            m_renderer->set_scene_clear_color(scene_clear_color);
            m_window->clear(scene_clear_color.r, scene_clear_color.g, scene_clear_color.b, scene_clear_color.a);
            on_render();
            m_scene_manager.render(*m_renderer);
        }
#else
        m_renderer->set_custom_render_target(nullptr);
        const SDL_Color scene_clear_color = m_scene_manager.get_clear_color();
        m_renderer->set_scene_clear_color(scene_clear_color);
        m_window->clear(scene_clear_color.r, scene_clear_color.g, scene_clear_color.b, scene_clear_color.a);
        on_render();
        m_scene_manager.render(*m_renderer);
#endif

        // 5. Render Editor Panels & Finalize ImGui Draw Data
#if ENGINE_WITH_EDITOR
        if (m_editor_layer) {
            m_editor_layer->render_panels(raw_delta_time);
            m_editor_layer->end_frame(m_window->get_sdl_renderer());
        }
#endif

        m_renderer->present();
        m_scene_manager.frame_profiler().end_frame();
    }

#if ENGINE_WITH_EDITOR
    if (m_editor_layer) {
        m_editor_layer->shutdown();
    }
#endif

    on_shutdown();
    AudioEngine::shutdown();
    Input::shutdown();
    ENGINE_LOG_INFO("Engine Application shutting down.");
    Log::set_callback(nullptr);
    Log::shutdown();
}

} // namespace Engine

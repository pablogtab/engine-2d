#include "Application.hpp"
#include "Log.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "../audio/AudioEngine.hpp"
#include "../editor/EditorLayer.hpp"
#include "../editor/EditorSelection.hpp"
#include "../editor/commands/EditorCommandHistory.hpp"
#include "../editor/panels/ViewportPanel.hpp"
#include "../editor/panels/SceneHierarchyPanel.hpp"
#include "../editor/panels/InspectorPanel.hpp"
#include "../editor/panels/ConsolePanel.hpp"
#include "../editor/panels/StatsPanel.hpp"
#include "../editor/panels/AssetBrowserPanel.hpp"
#include "../editor/panels/PostProcessTunerPanel.hpp"
#include "../editor/panels/ShortcutsPreferencesPanel.hpp"
#include "../editor/panels/TilemapPalettePanel.hpp"
#include "../editor/panels/SpriteAnimationStudioPanel.hpp"
#include "../scene/GenericScene.hpp"
#include "../scene/SceneSerializer.hpp"
#include "../ecs/TransformSystem.hpp"

namespace Engine {

Application::Application(const std::string& name, int width, int height) {
    Log::init("engine.log");
    ENGINE_LOG_INFO("Initializing Engine Application: '{}' ({}x{})", name, width, height);

    Input::init();
    AudioEngine::init();

    // 1. Create Window & SDL Renderer
    m_window = std::make_unique<Window>(name, width, height);
    m_renderer = std::make_unique<Renderer2D>(m_window->get_sdl_renderer());
    m_renderer->set_thread_pool(&m_thread_pool);

    // Initialize Editor Layer & Modular Panels
    m_editor_layer = std::make_unique<EditorLayer>();
    m_editor_layer->init(m_window->get_sdl_window(), m_window->get_sdl_renderer());
    m_editor_layer->set_scene_manager(&m_scene_manager);
    m_scene_manager.set_renderer(m_window->get_sdl_renderer());

    auto selection = std::make_shared<EditorSelection>();
    auto command_history = std::make_shared<EditorCommandHistory>();

    m_editor_layer->set_command_history(command_history);
    m_scene_manager.set_on_authoring_state_invalidated([command_history]() {
        command_history->clear();
    });

    auto viewport_panel = std::make_shared<ViewportPanel>(m_window->get_sdl_renderer(), &m_scene_manager, selection, command_history, 1920, 1080);
    auto hierarchy_panel = std::make_shared<SceneHierarchyPanel>(&m_scene_manager, m_window->get_sdl_renderer(), selection, command_history);
    auto inspector_panel = std::make_shared<InspectorPanel>(&m_scene_manager, selection, m_window->get_sdl_renderer());
    inspector_panel->set_command_history(command_history);
    auto console_panel = std::make_shared<ConsolePanel>();
    auto stats_panel = std::make_shared<StatsPanel>();
    auto asset_browser = std::make_shared<AssetBrowserPanel>("assets");
    auto postprocess_panel = std::make_shared<PostProcessTunerPanel>();
    postprocess_panel->is_open = false; // Closed by default at startup (accessible via View / Windows menu)
    auto shortcuts_panel = std::make_shared<ShortcutsPreferencesPanel>();
    shortcuts_panel->is_open = false; // Closed by default until opened from Edit menu
    auto palette_panel = std::make_shared<TilemapPalettePanel>(m_window->get_sdl_renderer(), &m_scene_manager, selection);

    asset_browser->set_on_load_scene_callback([this](const std::string& scene_path) {
        std::filesystem::path p(scene_path);
        std::string scene_name = p.stem().string();
        auto new_scene = std::make_shared<GenericScene>(scene_name);
        SceneSerializer serializer(new_scene.get(), m_window->get_sdl_renderer());
        if (serializer.deserialize(scene_path)) {
            m_scene_manager.add_scene(new_scene);
            m_scene_manager.set_active_scene(scene_name);
            if (m_editor_layer && m_editor_layer->is_visible()) {
                auto* vp = m_editor_layer->get_viewport_panel();
                if (vp) m_scene_manager.resize(vp->get_internal_width(), vp->get_internal_height(), 1.0f);
            }
        }
    });

    auto anim_studio_panel = std::make_shared<SpriteAnimationStudioPanel>(&m_scene_manager, selection);
    inspector_panel->set_animation_studio(anim_studio_panel.get());

    viewport_panel->set_palette_panel(palette_panel.get());
    m_editor_layer->set_viewport_panel(viewport_panel.get());
    m_editor_layer->register_panel(viewport_panel);
    m_editor_layer->register_panel(hierarchy_panel);
    m_editor_layer->register_panel(inspector_panel);
    m_editor_layer->register_panel(console_panel);
    m_editor_layer->register_panel(asset_browser);
    m_editor_layer->register_panel(stats_panel);
    m_editor_layer->register_panel(postprocess_panel);
    m_editor_layer->register_panel(shortcuts_panel);
    m_editor_layer->register_panel(palette_panel);
    m_editor_layer->register_panel(anim_studio_panel);

    // Connect Log stream to in-game ConsolePanel
    Log::set_callback([console_panel](const std::string& timestamp, const std::string& level, const std::string& message) {
        console_panel->add_log(timestamp, level, message);
    });

    // Event routing: Pass events through EditorLayer and Input Manager (isolated via Viewport focus)
    m_window->set_event_callback([this](const SDL_Event& event) {
        if (this->m_editor_layer) {
            this->m_editor_layer->process_event(event);
        }
        Input::process_event(event);
    });

    // Register window resize / High-DPI scale change callback
    m_window->set_resize_callback([this](int w, int h, float dpi) {
        this->on_resize(w, h);
        if (this->m_editor_layer && this->m_editor_layer->is_visible()) {
            auto* vp = this->m_editor_layer->get_viewport_panel();
            if (vp) {
                this->m_scene_manager.resize(vp->get_internal_width(), vp->get_internal_height(), 1.0f);
                return;
            }
        }
        this->m_scene_manager.resize(w, h, dpi);
    });
}

Application::~Application() {}

void Application::run() {
    on_init();

    // Trigger initial on_resize
    int pixel_w = m_window->get_pixel_width();
    int pixel_h = m_window->get_pixel_height();
    float dpi   = m_window->get_dpi_scale();
    on_resize(pixel_w, pixel_h);

    if (m_editor_layer && m_editor_layer->is_visible()) {
        auto* vp = m_editor_layer->get_viewport_panel();
        if (vp) {
            m_scene_manager.resize(vp->get_internal_width(), vp->get_internal_height(), 1.0f);
        } else {
            m_scene_manager.resize(pixel_w, pixel_h, dpi);
        }
    } else {
        m_scene_manager.resize(pixel_w, pixel_h, dpi);
    }

    uint64_t last_counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();
    bool last_editor_visible = m_editor_layer ? m_editor_layer->is_visible() : false;

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

        // Handle Editor visibility change (F1 key toggle)
        if (m_editor_layer) {
            bool is_vis = m_editor_layer->is_visible();
            if (is_vis != last_editor_visible) {
                last_editor_visible = is_vis;
                if (is_vis) {
                    auto* vp = m_editor_layer->get_viewport_panel();
                    if (vp) m_scene_manager.resize(vp->get_internal_width(), vp->get_internal_height(), 1.0f);
                } else {
                    m_scene_manager.resize(m_window->get_pixel_width(), m_window->get_pixel_height(), m_window->get_dpi_scale());
                }
            }
        }

        // 2. Begin Editor Frame (DockSpace & MenuBar)
        if (m_editor_layer) {
            m_editor_layer->begin_frame();
            m_editor_layer->render_dockspace();
        }

        // Handle TAB / Shift+TAB for cycling demo scenes when in game mode or not typing in an ImGui input field
        bool is_imgui_typing = m_editor_layer && m_editor_layer->is_visible() && m_editor_layer->want_text_input();
        if (!is_imgui_typing && Input::is_key_just_pressed(SDL_SCANCODE_TAB)) {
            if (Input::is_key_pressed(SDL_SCANCODE_LSHIFT) || Input::is_key_pressed(SDL_SCANCODE_RSHIFT)) {
                m_scene_manager.previous_scene();
            } else {
                m_scene_manager.next_scene();
            }
            if (m_editor_layer && m_editor_layer->is_visible()) {
                auto* vp = m_editor_layer->get_viewport_panel();
                if (vp) m_scene_manager.resize(vp->get_internal_width(), vp->get_internal_height(), 1.0f);
            }
        }

        // Configure Input isolation based on Viewport focus/hover state
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

        // 3. Update Application logic & active Scene (scaled delta time)
        bool is_playing = true;
        if (m_editor_layer && m_editor_layer->is_visible()) {
            auto* vp = m_editor_layer->get_viewport_panel();
            if (vp) {
                is_playing = (vp->get_play_state() == EditorPlayState::Play);
            }
        }

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

        // 4. Render Game Scene into Viewport Render Target (if editor is active) or to window
        ViewportPanel* vp = m_editor_layer ? m_editor_layer->get_viewport_panel() : nullptr;
        if (vp && m_editor_layer->is_visible()) {
            m_renderer->set_custom_render_target(vp->get_viewport_texture());
            vp->begin_render();
            on_render();
            m_scene_manager.render(*m_renderer);
            vp->end_render();
            m_renderer->set_custom_render_target(nullptr);

            // Clear main backbuffer with dark editor background
            m_window->clear(23, 26, 33, 255);
        } else {
            m_renderer->set_custom_render_target(nullptr);
            m_window->clear(24, 28, 36, 255);
            on_render();
            m_scene_manager.render(*m_renderer);
        }

        // 5. Render Editor Panels & Finalize ImGui Draw Data
        if (m_editor_layer) {
            m_editor_layer->render_panels(raw_delta_time);
            m_editor_layer->end_frame(m_window->get_sdl_renderer());
        }

        m_window->present();
    }

    if (m_editor_layer) {
        m_editor_layer->shutdown();
    }

    on_shutdown();
    AudioEngine::shutdown();
    Input::shutdown();
    ENGINE_LOG_INFO("Engine Application shutting down.");
    Log::shutdown();
}

} // namespace Engine

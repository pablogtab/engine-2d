#include "Application.hpp"
#include "Log.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "../audio/AudioEngine.hpp"
#include "../editor/EditorLayer.hpp"
#include "../editor/panels/ViewportPanel.hpp"
#include "../editor/panels/SceneHierarchyPanel.hpp"
#include "../editor/panels/InspectorPanel.hpp"
#include "../editor/panels/ConsolePanel.hpp"
#include "../editor/panels/StatsPanel.hpp"

namespace Engine {

Application::Application(const std::string& name, int width, int height) {
    Log::init("engine.log");
    ENGINE_LOG_INFO("Initializing Engine Application: '{}' ({}x{})", name, width, height);

    Input::init();
    AudioEngine::init();

    m_window = std::make_unique<Window>(name, width, height);
    m_renderer = std::make_unique<Renderer2D>(m_window->get_sdl_renderer());
    m_renderer->set_thread_pool(&m_thread_pool);

    // Initialize Editor Layer & Modular Panels
    m_editor_layer = std::make_unique<EditorLayer>();
    m_editor_layer->init(m_window->get_sdl_window(), m_window->get_sdl_renderer());

    auto viewport_panel = std::make_shared<ViewportPanel>(m_window->get_sdl_renderer(), &m_scene_manager, 1920, 1080);
    auto hierarchy_panel = std::make_shared<SceneHierarchyPanel>(&m_scene_manager);
    auto inspector_panel = std::make_shared<InspectorPanel>(&m_scene_manager, hierarchy_panel.get());
    auto console_panel = std::make_shared<ConsolePanel>();
    auto stats_panel = std::make_shared<StatsPanel>();

    m_editor_layer->set_viewport_panel(viewport_panel.get());
    m_editor_layer->register_panel(viewport_panel);
    m_editor_layer->register_panel(hierarchy_panel);
    m_editor_layer->register_panel(inspector_panel);
    m_editor_layer->register_panel(console_panel);
    m_editor_layer->register_panel(stats_panel);

    // Connect Log stream to in-game ConsolePanel
    Log::set_callback([console_panel](const std::string& timestamp, const std::string& level, const std::string& message) {
        console_panel->add_log(timestamp, level, message);
    });

    // Event routing: Pass events through EditorLayer first, then to Game Input if not captured
    m_window->set_event_callback([this](const SDL_Event& event) {
        if (this->m_editor_layer) {
            this->m_editor_layer->process_event(event);
        }

        bool mouse_event = (event.type == SDL_EVENT_MOUSE_MOTION ||
                            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                            event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                            event.type == SDL_EVENT_MOUSE_WHEEL);
        bool key_event = (event.type == SDL_EVENT_KEY_DOWN ||
                          event.type == SDL_EVENT_KEY_UP ||
                          event.type == SDL_EVENT_TEXT_INPUT);

        bool captured = (mouse_event && this->m_editor_layer && this->m_editor_layer->want_capture_mouse()) ||
                        (key_event && this->m_editor_layer && this->m_editor_layer->want_capture_keyboard());

        if (!captured) {
            Input::process_event(event);
        }
    });

    // Register window resize / High-DPI scale change callback
    m_window->set_resize_callback([this](int w, int h, float dpi) {
        this->on_resize(w, h);
        this->m_scene_manager.resize(w, h, dpi);
    });
}

Application::~Application() {}

void Application::run() {
    on_init();

    // Trigger initial on_resize with current render target pixel size and DPI scale
    int pixel_w = m_window->get_pixel_width();
    int pixel_h = m_window->get_pixel_height();
    float dpi   = m_window->get_dpi_scale();
    on_resize(pixel_w, pixel_h);
    m_scene_manager.resize(pixel_w, pixel_h, dpi);

    uint64_t last_counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();

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

        // 2. Begin Editor Frame (DockSpace & MenuBar)
        if (m_editor_layer) {
            m_editor_layer->begin_frame();
            m_editor_layer->render_dockspace();
        }

        // 3. Update Application logic & active Scene (scaled delta time)
        on_update(dt);
        m_scene_manager.update(dt);

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

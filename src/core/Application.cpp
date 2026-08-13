#include "Application.hpp"
#include "Log.hpp"
#include "Input.hpp"

namespace Engine {

Application::Application(const std::string& name, int width, int height) {
    Log::init("engine.log");
    ENGINE_LOG_INFO("Initializing Engine Application: '{}' ({}x{})", name, width, height);

    Input::init();

    m_window = std::make_unique<Window>(name, width, height);
    m_renderer = std::make_unique<Renderer2D>(m_window->get_sdl_renderer());
    m_renderer->set_thread_pool(&m_thread_pool);

    // Register window resize / High-DPI scale change callback
    m_window->set_resize_callback([this](int w, int h) {
        this->on_resize(w, h);
        this->m_scene_manager.resize(w, h);
    });
}

void Application::run() {
    on_init();

    // Trigger initial on_resize with current render target pixel size
    int pixel_w = m_window->get_pixel_width();
    int pixel_h = m_window->get_pixel_height();
    on_resize(pixel_w, pixel_h);
    m_scene_manager.resize(pixel_w, pixel_h);

    uint64_t last_counter = SDL_GetPerformanceCounter();
    uint64_t frequency = SDL_GetPerformanceFrequency();

    while (m_running && !m_window->is_should_close()) {
        uint64_t current_counter = SDL_GetPerformanceCounter();
        float delta_time = static_cast<float>(current_counter - last_counter) / static_cast<float>(frequency);
        last_counter = current_counter;

        // Cap delta_time to a reasonable maximum (e.g. 0.1s)
        if (delta_time > 0.1f) {
            delta_time = 0.1f;
        }

        // 1. Reset frame input delta states, then process window & input events
        Input::update();
        m_window->poll_events();

        // Scene Switching shortcut (Press TAB)
        if (Input::is_key_just_pressed(SDL_SCANCODE_TAB)) {
            m_scene_manager.next_scene();
        }

        // 2. Update Application logic & active Scene
        on_update(delta_time);
        m_scene_manager.update(delta_time);

        // 3. Render frame & active Scene
        m_window->clear(24, 28, 36, 255);
        on_render();
        m_scene_manager.render(*m_renderer);
        m_window->present();
    }

    on_shutdown();
    Input::shutdown();
    ENGINE_LOG_INFO("Engine Application shutting down.");
    Log::shutdown();
}

} // namespace Engine

#pragma once

#include "../renderer/RenderView.hpp"
#include <SDL3/SDL.h>
#include <string>
#include <functional>

namespace Engine {

class Window {
public:
    Window(const std::string& title, int width, int height, bool persist_editor_state = true);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool is_should_close() const { return m_should_close; }
    void poll_events();
    void clear(uint8_t r = 20, uint8_t g = 24, uint8_t b = 32, uint8_t a = 255);
    void present();
    void set_logical_presentation(int reference_width, int reference_height, PresentationMode mode);
    void disable_logical_presentation();

    SDL_Window* get_sdl_window() const { return m_window; }
    SDL_Renderer* get_sdl_renderer() const { return m_renderer; }

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    int get_pixel_width() const { return m_pixel_width; }
    int get_pixel_height() const { return m_pixel_height; }
    float get_dpi_scale() const {
        return (m_height > 0) ? (static_cast<float>(m_pixel_height) / static_cast<float>(m_height)) : 1.0f;
    }

    void set_display_change_callback(std::function<void()> callback) {
        m_display_change_callback = std::move(callback);
    }

    void set_event_callback(std::function<void(const SDL_Event&)> callback) {
        m_event_callback = callback;
    }

private:
    void update_sizes();
    void save_state();
    bool load_state(int& x, int& y, int& w, int& h);

    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    std::string m_title;
    int m_width{0};
    int m_height{0};
    int m_pixel_width{0};
    int m_pixel_height{0};
    bool m_should_close{false};
    bool m_uses_logical_presentation{false};
    bool m_persist_editor_state{true};
    std::function<void()> m_display_change_callback;
    std::function<void(const SDL_Event&)> m_event_callback;
};

} // namespace Engine

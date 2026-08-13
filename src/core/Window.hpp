#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <functional>

namespace Engine {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool is_should_close() const { return m_should_close; }
    void poll_events();
    void clear(uint8_t r = 20, uint8_t g = 24, uint8_t b = 32, uint8_t a = 255);
    void present();

    SDL_Window* get_sdl_window() const { return m_window; }
    SDL_Renderer* get_sdl_renderer() const { return m_renderer; }

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    int get_pixel_width() const { return m_pixel_width; }
    int get_pixel_height() const { return m_pixel_height; }

    void set_resize_callback(std::function<void(int, int)> callback) {
        m_resize_callback = callback;
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
    std::function<void(int, int)> m_resize_callback;
};

} // namespace Engine

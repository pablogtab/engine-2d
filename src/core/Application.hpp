#pragma once

#include "Window.hpp"
#include "renderer/Renderer2D.hpp"
#include "scene/SceneManager.hpp"
#include <memory>

namespace Engine {

class Application {
public:
    Application(const std::string& name = "2D Engine", int width = 1280, int height = 720);
    virtual ~Application() = default;

    void run();

    SceneManager& get_scene_manager() { return m_scene_manager; }
    Window& get_window() { return *m_window; }
    Renderer2D* get_renderer() { return m_renderer.get(); }

protected:
    virtual void on_init() {}
    virtual void on_update(float delta_time) {}
    virtual void on_render() {}
    virtual void on_resize(int width, int height) {}
    virtual void on_shutdown() {}

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer2D> m_renderer;
    SceneManager m_scene_manager;
    bool m_running{true};
    bool m_tab_pressed_last_frame{false};
};

} // namespace Engine

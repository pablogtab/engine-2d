#pragma once

#include "Window.hpp"
#include "AssetManager.hpp"
#include "ThreadPool.hpp"
#include "ProjectSettings.hpp"
#include "renderer/Renderer2D.hpp"
#include "scene/SceneManager.hpp"
#include <memory>

namespace Engine {

class Application {
public:
    Application(const std::string& name = "2D Engine", int width = 1280, int height = 720);
    virtual ~Application();

    void run();

    SceneManager& get_scene_manager() { return m_scene_manager; }
    AssetManager& get_asset_manager() { return m_asset_manager; }
    ThreadPool& get_thread_pool() { return m_thread_pool; }
    Window& get_window() { return *m_window; }
    Renderer2D* get_renderer() { return m_renderer.get(); }
    class EditorLayer* get_editor_layer() { return m_editor_layer.get(); }

protected:
    virtual void on_init() {}
    virtual void on_update(float delta_time) {}
    virtual void on_render() {}
    virtual void on_resize(int width, int height) {}
    virtual void on_shutdown() {}

private:
    void apply_display_mode(bool editor_visible);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer2D> m_renderer;
    std::unique_ptr<class EditorLayer> m_editor_layer;
    SceneManager m_scene_manager;
    AssetManager m_asset_manager;
    ThreadPool m_thread_pool;
    ProjectSettings m_project_settings;
    bool m_running{true};
};

} // namespace Engine

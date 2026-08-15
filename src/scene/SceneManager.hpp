#pragma once

#include "Scene.hpp"
#include <vector>
#include <memory>
#include <string>

namespace Engine {

class SceneManager {
public:
    SceneManager() = default;

    void add_scene(std::shared_ptr<Scene> scene);
    void set_active_scene(size_t index);
    void set_active_scene(const std::string& name);
    void next_scene();
    void previous_scene();

    void restore_state();

    void update(float delta_time);
    void render(Renderer2D& renderer);
    void resize(int width, int height, float dpi_scale = 1.0f);

    Scene* get_active_scene() const;
    size_t get_scene_count() const { return m_scenes.size(); }
    int get_active_index() const { return m_active_index; }
    [[nodiscard]] const std::vector<std::shared_ptr<Scene>>& get_scenes() const { return m_scenes; }

private:
    void save_state();

    std::vector<std::shared_ptr<Scene>> m_scenes;
    int m_active_index{-1};
    int m_last_width{1280};
    int m_last_height{720};
    float m_last_dpi_scale{1.0f};
};

} // namespace Engine

#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include <string>
#include <memory>

namespace Engine {

class SceneManager;

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel(SceneManager* scene_manager, std::shared_ptr<EditorSelection> selection = nullptr, struct SDL_Renderer* renderer = nullptr);
    ~InspectorPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = selection; }
    void set_renderer(struct SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_animation_studio(class SpriteAnimationStudioPanel* studio) { m_anim_studio = studio; }

private:
    void draw_components(EntityManager& em, EntityID entity);

    std::string m_name{"Entity Inspector"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    struct SDL_Renderer* m_renderer{nullptr};
    class SpriteAnimationStudioPanel* m_anim_studio{nullptr};
};

} // namespace Engine

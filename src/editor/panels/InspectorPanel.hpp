#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include "../inspector/AnimationInspectorDrawer.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <string>
#include <memory>

namespace Engine {

class SceneManager;
class AssetManager;

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel(
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection = nullptr,
        struct SDL_Renderer* renderer = nullptr,
        AssetManager* asset_manager = nullptr
    );
    ~InspectorPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = selection; }
    void set_renderer(struct SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) { m_command_history = command_history; }
    void set_animation_studio(class SpriteAnimationStudioPanel* studio) { m_anim_studio = studio; }

private:
    void draw_components(EntityManager& em, EntityID entity);
    void draw_multi_components(EntityManager& em, const std::vector<EntityID>& entities);
    void track_tag_edit(EntityManager& em, EntityID entity, const std::string& before, const char* property, const char* description);

    std::string m_name{"Entity Inspector"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    EditorPropertyTransaction m_property_transactions;
    AnimationPreviewState m_animation_preview;
    struct SDL_Renderer* m_renderer{nullptr};
    AssetManager* m_asset_manager{nullptr};
    class SpriteAnimationStudioPanel* m_anim_studio{nullptr};
};

} // namespace Engine

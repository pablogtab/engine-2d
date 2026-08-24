#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include "../../core/AssetHandle.hpp"
#include <imgui.h>
#include <string>
#include <memory>

struct SDL_Renderer;
struct SDL_Texture;

namespace Engine {

class Scene;
class SceneManager;
class AssetManager;

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel(
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection = nullptr,
        SDL_Renderer* renderer = nullptr,
        AssetManager* asset_manager = nullptr
    );
    ~InspectorPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = selection; }
    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) { m_command_history = command_history; }
    void set_entity_state_store(std::shared_ptr<class EditorEntityStateStore> store) { m_entity_state_store = store; }
    [[nodiscard]] std::shared_ptr<class EditorEntityStateStore> get_entity_state_store() const { return m_entity_state_store; }
    void set_animation_studio(class SpriteAnimationStudioPanel* studio) { m_anim_studio = studio; }
    void set_prefab_stage(class PrefabStageController* stage) { m_prefab_stage = stage; }
    [[nodiscard]] class PrefabStageController* get_prefab_stage() const { return m_prefab_stage; }
    void set_on_open_prefab_callback(std::function<void(const std::string&)> cb) { m_on_open_prefab = std::move(cb); }
    void set_material_asset_actions(
        std::function<AssetHandle()> create,
        std::function<AssetHandle(AssetHandle)> duplicate,
        std::function<void(AssetHandle)> open,
        std::function<SDL_Texture*(AssetHandle)> preview
    ) {
        m_create_material = std::move(create);
        m_duplicate_material = std::move(duplicate);
        m_open_material = std::move(open);
        m_preview_material = std::move(preview);
    }

private:
    void draw_components(EntityManager& em, EntityID entity);
    void draw_multi_components(EntityManager& em, const std::vector<EntityID>& entities);
    void draw_scene_settings(Scene& active_scene);
    void track_tag_edit(EntityManager& em, EntityID entity, const std::string& before, const char* property, const char* description);

    std::string m_name{"Entity Inspector"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    std::shared_ptr<class EditorEntityStateStore> m_entity_state_store{nullptr};
    EditorPropertyTransaction m_property_transactions;
    SDL_Renderer* m_renderer{nullptr};
    AssetManager* m_asset_manager{nullptr};
    class SpriteAnimationStudioPanel* m_anim_studio{nullptr};
    class PrefabStageController* m_prefab_stage{nullptr};
    std::function<void(const std::string&)> m_on_open_prefab;
    std::function<AssetHandle()> m_create_material;
    std::function<AssetHandle(AssetHandle)> m_duplicate_material;
    std::function<void(AssetHandle)> m_open_material;
    std::function<SDL_Texture*(AssetHandle)> m_preview_material;
    char m_multi_tag_buffer[128]{""};
    std::uint32_t m_multi_tag_selection_owner{0};
    int m_layer_effect_target_layer{0};
};



} // namespace Engine

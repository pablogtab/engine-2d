#pragma once

#include "../ecs/EntityManager.hpp"
#include "../events/EventDispatcher.hpp"
#include "../core/AssetHandle.hpp"
#include "../renderer/RenderLayerEffectProfile.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

struct SceneSettings {
    bool physics_enabled{true};
    glm::vec2 gravity{0.0f, -980.0f};
    AssetHandle post_process_profile_handle{};
    std::vector<RenderLayerEffectAssignment> render_layer_effect_assignments;
};

class Scene {
public:
    explicit Scene(std::string name = "Untitled Scene") : m_name(std::move(name)) {}
    virtual ~Scene() = default;

    const std::string& get_name() const { return m_name; }

    EntityManager& get_entity_manager() { return m_entity_manager; }
    const EntityManager& get_entity_manager() const { return m_entity_manager; }
    void swap_entity_manager(EntityManager& replacement) noexcept { m_entity_manager.swap(replacement); }
    EventDispatcher& get_event_dispatcher() { return m_event_dispatcher; }
    [[nodiscard]] SceneSettings& settings() { return m_settings; }
    [[nodiscard]] const SceneSettings& settings() const { return m_settings; }
    [[nodiscard]] AssetHandle get_post_process_profile_handle() const { return m_settings.post_process_profile_handle; }
    void set_post_process_profile_handle(AssetHandle handle) { m_settings.post_process_profile_handle = handle; }
    [[nodiscard]] const std::vector<RenderLayerEffectAssignment>& get_render_layer_effect_assignments() const {
        return m_settings.render_layer_effect_assignments;
    }
    void set_render_layer_effect_assignments(std::vector<RenderLayerEffectAssignment> assignments) {
        m_settings.render_layer_effect_assignments = std::move(assignments);
    }

protected:
    EntityManager m_entity_manager;
    EventDispatcher m_event_dispatcher;

private:
    std::string m_name;
    SceneSettings m_settings;
};

} // namespace Engine

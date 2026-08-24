#pragma once

#include "../ecs/EntityManager.hpp"
#include "../events/EventDispatcher.hpp"
#include <glm/glm.hpp>
#include <string>

namespace Engine {

struct SceneSettings {
    bool physics_enabled{true};
    glm::vec2 gravity{0.0f, -980.0f};

    bool operator==(const SceneSettings& other) const = default;
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
    void set_settings(SceneSettings settings) { m_settings = std::move(settings); }

protected:
    EntityManager m_entity_manager;
    EventDispatcher m_event_dispatcher;

private:
    std::string m_name;
    SceneSettings m_settings;
};

} // namespace Engine

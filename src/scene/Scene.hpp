#pragma once

#include "../ecs/EntityManager.hpp"
#include "../events/EventDispatcher.hpp"

namespace Engine {

class Renderer2D;

class Scene {
public:
    virtual ~Scene() = default;

    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_update(float delta_time) {}
    virtual void on_render(Renderer2D& renderer) {}
    virtual void on_resize(int width, int height) {}
    virtual void on_resize(int width, int height, float dpi_scale) { on_resize(width, height); }

    virtual void set_is_editing(bool is_editing) {}
    [[nodiscard]] virtual bool is_editing() const { return true; }

    const std::string& get_name() const { return m_name; }

    EntityManager& get_entity_manager() { return m_entity_manager; }
    EventDispatcher& get_event_dispatcher() { return m_event_dispatcher; }

protected:
    explicit Scene(std::string name) : m_name(std::move(name)) {}

    EntityManager m_entity_manager;
    EventDispatcher m_event_dispatcher;

private:
    std::string m_name;
};

} // namespace Engine

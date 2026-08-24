#pragma once

#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <string>
#include <cstdint>
#include <typeindex>
#include <utility>

namespace Engine {

using EntityID = uint32_t;

// Base Event Interface
struct IEvent {
    virtual ~IEvent() = default;
    virtual const char* get_name() const = 0;
    virtual std::type_index get_type_index() const = 0;
};

// Built-in Common Engine Events
struct EntityDestroyedEvent : public IEvent {
    EntityID entity;

    explicit EntityDestroyedEvent(EntityID id) : entity(id) {}
    const char* get_name() const override { return "EntityDestroyedEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(EntityDestroyedEvent)); }
};

struct CollisionEvent : public IEvent {
    EntityID entity_a;
    EntityID entity_b;
    glm::vec2 contact_normal;
    bool is_trigger{false};

    CollisionEvent(EntityID a, EntityID b, const glm::vec2& contact = {0.0f, 0.0f}, bool trigger = false)
        : entity_a(a), entity_b(b), contact_normal(contact), is_trigger(trigger) {}

    const char* get_name() const override { return "CollisionEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(CollisionEvent)); }
};

struct CollisionEndEvent : public IEvent {
    EntityID entity_a;
    EntityID entity_b;
    bool is_trigger{false};

    CollisionEndEvent(EntityID a, EntityID b, bool trigger = false)
        : entity_a(a), entity_b(b), is_trigger(trigger) {}

    const char* get_name() const override { return "CollisionEndEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(CollisionEndEvent)); }
};

struct CustomLogEvent : public IEvent {
    std::string message;

    explicit CustomLogEvent(const std::string& msg) : message(msg) {}
    const char* get_name() const override { return "CustomLogEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(CustomLogEvent)); }
};

struct ScriptEvent : public IEvent {
    EntityID source{0};
    std::string name;
    std::string payload;

    ScriptEvent(EntityID source_id, std::string event_name, std::string event_payload)
        : source(source_id), name(std::move(event_name)), payload(std::move(event_payload)) {}
    const char* get_name() const override { return "ScriptEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(ScriptEvent)); }
};

struct UIButtonClickedEvent : public IEvent {
    EntityID entity;

    explicit UIButtonClickedEvent(EntityID id) : entity(id) {}
    const char* get_name() const override { return "UIButtonClickedEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(UIButtonClickedEvent)); }
};

struct UIFocusChangedEvent : public IEvent {
    EntityID previous_entity;
    EntityID focused_entity;

    UIFocusChangedEvent(EntityID previous, EntityID focused)
        : previous_entity(previous), focused_entity(focused) {}
    const char* get_name() const override { return "UIFocusChangedEvent"; }
    std::type_index get_type_index() const override { return std::type_index(typeid(UIFocusChangedEvent)); }
};

} // namespace Engine

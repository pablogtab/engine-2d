#pragma once

#include "../ecs/EntityManager.hpp"
#include <any>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

struct SDL_Renderer;

namespace Engine {

class AssetManager;

class ComponentSnapshotSet {
public:
    template<typename Component>
    void set(Component component) {
        m_components.insert_or_assign(std::type_index(typeid(Component)), std::move(component));
    }

    template<typename Component>
    [[nodiscard]] bool contains() const {
        return m_components.contains(std::type_index(typeid(Component)));
    }

    template<typename Component>
    [[nodiscard]] Component* get() {
        auto found = m_components.find(std::type_index(typeid(Component)));
        return found == m_components.end() ? nullptr : std::any_cast<Component>(&found->second);
    }

    template<typename Component>
    [[nodiscard]] const Component* get() const {
        auto found = m_components.find(std::type_index(typeid(Component)));
        return found == m_components.end() ? nullptr : std::any_cast<Component>(&found->second);
    }

    [[nodiscard]] std::size_t size() const { return m_components.size(); }
    [[nodiscard]] bool empty() const { return m_components.empty(); }

private:
    std::unordered_map<std::type_index, std::any> m_components;
};

struct ComponentSerializationContext {
    SDL_Renderer* renderer{nullptr};
    AssetManager* asset_manager{nullptr};
};

enum class ComponentFinalizeReason {
    Deserialization,
    SnapshotRestore,
    Clone,
    DefaultCreation
};

struct ComponentFinalizeContext {
    ComponentSerializationContext serialization;
    ComponentFinalizeReason reason{ComponentFinalizeReason::Deserialization};
};

struct ComponentValidationIssue {
    std::string component_key;
    std::string message;
};

struct ComponentDescriptor {
    std::string display_name;
    std::string serialized_key;
    std::uint32_t schema_version{1};
    std::type_index type{typeid(void)};
    bool required{false};
    bool removable{true};

    std::function<bool(const EntityManager&, EntityID)> has;
    std::function<void(EntityManager&, EntityID)> add_default;
    std::function<void(EntityManager&, EntityID)> remove;
    std::function<void(EntityManager&, EntityID, EntityID)> clone;
    std::function<void(EntityManager&, EntityID, ComponentSnapshotSet&)> capture;
    std::function<void(EntityManager&, EntityID, const ComponentSnapshotSet&)> restore;
    std::function<nlohmann::json(const EntityManager&, EntityID, const ComponentSerializationContext&)> serialize;
    std::function<bool(
        EntityManager&,
        EntityID,
        const nlohmann::json&,
        const ComponentSerializationContext&,
        std::string&
    )> deserialize;
    std::function<bool(
        EntityManager&,
        EntityID,
        const ComponentFinalizeContext&,
        std::string&
    )> finalize;
    std::function<void(EntityManager&, EntityID)> after_remove;
    std::function<std::optional<std::string>(const EntityManager&, EntityID)> validate;
};

template<typename Component>
struct TypedComponentRegistration {
    using Json = nlohmann::json;

    std::string display_name;
    std::string serialized_key;
    std::uint32_t schema_version{1};
    bool required{false};
    bool removable{true};
    std::function<Json(const Component&, const ComponentSerializationContext&)> serialize;
    std::function<bool(Component&, const Json&, const ComponentSerializationContext&, std::string&)> deserialize;
    std::function<bool(
        EntityManager&,
        EntityID,
        Component&,
        const ComponentFinalizeContext&,
        std::string&
    )> finalize;
    std::function<void(EntityManager&, EntityID)> after_remove;
    std::function<std::optional<std::string>(const Component&)> validate;
};

class ComponentRegistry {
public:
    template<typename Component>
    bool register_component(TypedComponentRegistration<Component> registration, std::string* error = nullptr) {
        if (!registration.serialize || !registration.deserialize) {
            if (error) *error = "Typed component registration is missing serialization operations";
            return false;
        }
        ComponentDescriptor descriptor;
        descriptor.display_name = std::move(registration.display_name);
        descriptor.serialized_key = std::move(registration.serialized_key);
        descriptor.schema_version = registration.schema_version;
        descriptor.type = std::type_index(typeid(Component));
        descriptor.required = registration.required;
        descriptor.removable = registration.removable;
        descriptor.has = [](const EntityManager& em, EntityID entity) {
            return em.has_component<Component>(entity);
        };
        descriptor.add_default = [](EntityManager& em, EntityID entity) {
            if (!em.has_component<Component>(entity)) em.add_component<Component>(entity, Component{});
        };
        descriptor.remove = [after_remove = registration.after_remove](EntityManager& em, EntityID entity) {
            if (!em.has_component<Component>(entity)) return;
            em.remove_component<Component>(entity);
            if (after_remove) after_remove(em, entity);
        };
        descriptor.clone = [](EntityManager& em, EntityID source, EntityID target) {
            if (!em.has_component<Component>(source)) return;
            const Component component = em.get_component<Component>(source);
            if (em.has_component<Component>(target)) {
                em.get_component<Component>(target) = component;
            } else {
                em.add_component<Component>(target, component);
            }
        };
        descriptor.capture = [](EntityManager& em, EntityID entity, ComponentSnapshotSet& snapshot) {
            if (em.has_component<Component>(entity)) snapshot.set(em.get_component<Component>(entity));
        };
        descriptor.restore = [after_remove = registration.after_remove](
            EntityManager& em,
            EntityID entity,
            const ComponentSnapshotSet& snapshot
        ) {
            const Component* component = snapshot.get<Component>();
            if (component) {
                if (em.has_component<Component>(entity)) {
                    em.get_component<Component>(entity) = *component;
                } else {
                    em.add_component<Component>(entity, *component);
                }
            } else if (em.has_component<Component>(entity)) {
                em.remove_component<Component>(entity);
                if (after_remove) after_remove(em, entity);
            }
        };
        descriptor.serialize = [serialize = std::move(registration.serialize)](
            const EntityManager& em,
            EntityID entity,
            const ComponentSerializationContext& context
        ) {
            return serialize(em.get_component<Component>(entity), context);
        };
        descriptor.deserialize = [deserialize = std::move(registration.deserialize)](
            EntityManager& em,
            EntityID entity,
            const nlohmann::json& value,
            const ComponentSerializationContext& context,
            std::string& deserialize_error
        ) {
            Component component{};
            if (!deserialize(component, value, context, deserialize_error)) return false;
            if (em.has_component<Component>(entity)) {
                em.get_component<Component>(entity) = std::move(component);
            } else {
                em.add_component<Component>(entity, std::move(component));
            }
            return true;
        };
        if (registration.finalize) {
            descriptor.finalize = [finalize = std::move(registration.finalize)](
                EntityManager& em,
                EntityID entity,
                const ComponentFinalizeContext& context,
                std::string& finalize_error
            ) {
                return finalize(
                    em,
                    entity,
                    em.get_component<Component>(entity),
                    context,
                    finalize_error
                );
            };
        }
        descriptor.after_remove = std::move(registration.after_remove);
        if (registration.validate) {
            descriptor.validate = [validate = std::move(registration.validate)](
                const EntityManager& em,
                EntityID entity
            ) {
                return validate(em.get_component<Component>(entity));
            };
        }
        return register_descriptor(std::move(descriptor), error);
    }

    bool register_descriptor(ComponentDescriptor descriptor, std::string* error = nullptr);

    [[nodiscard]] const ComponentDescriptor* find_by_key(std::string_view key) const;
    [[nodiscard]] const ComponentDescriptor* find_by_type(std::type_index type) const;
    [[nodiscard]] const std::vector<ComponentDescriptor>& descriptors() const { return m_descriptors; }

    void capture_entity(EntityManager& em, EntityID entity, ComponentSnapshotSet& snapshot) const;
    void restore_entity(EntityManager& em, EntityID entity, const ComponentSnapshotSet& snapshot) const;
    void clone_entity_components(EntityManager& em, EntityID source, EntityID target) const;
    [[nodiscard]] bool add_default_component(
        std::string_view key,
        EntityManager& em,
        EntityID entity,
        std::string& error
    ) const;
    [[nodiscard]] bool remove_component(
        std::string_view key,
        EntityManager& em,
        EntityID entity,
        std::string& error
    ) const;
    [[nodiscard]] bool serialize_entity(
        const EntityManager& em,
        EntityID entity,
        nlohmann::json& entity_json,
        const ComponentSerializationContext& context,
        std::string& error
    ) const;
    [[nodiscard]] bool deserialize_entity(
        EntityManager& em,
        EntityID entity,
        const nlohmann::json& entity_json,
        const ComponentSerializationContext& context,
        std::string& error
    ) const;
    [[nodiscard]] bool finalize_entity(
        EntityManager& em,
        EntityID entity,
        const ComponentFinalizeContext& context,
        std::string& error
    ) const;
    [[nodiscard]] bool finalize_entities(
        EntityManager& em,
        const std::vector<EntityID>& entities,
        const ComponentFinalizeContext& context,
        std::string& error
    ) const;
    [[nodiscard]] std::vector<ComponentValidationIssue> validate_entity(
        const EntityManager& em,
        EntityID entity
    ) const;

private:
    std::vector<ComponentDescriptor> m_descriptors;
    std::unordered_map<std::string, std::size_t> m_by_key;
    std::unordered_map<std::type_index, std::size_t> m_by_type;
};

ComponentRegistry& default_component_registry();

} // namespace Engine

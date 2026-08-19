#include "ComponentRegistry.hpp"
#include "../core/AssetManager.hpp"
#include "../ecs/Components.hpp"
#include "../renderer/Texture.hpp"
#include <algorithm>
#include <exception>
#include <stdexcept>

namespace Engine {

bool ComponentRegistry::register_descriptor(ComponentDescriptor descriptor, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (descriptor.display_name.empty()) return fail("Component display name cannot be empty");
    if (descriptor.serialized_key.empty()) return fail("Component serialized key cannot be empty");
    if (descriptor.schema_version == 0) return fail("Component schema version must be greater than zero");
    if (descriptor.type == std::type_index(typeid(void))) return fail("Component runtime type is invalid");
    if (!descriptor.has || !descriptor.add_default || !descriptor.remove || !descriptor.clone ||
        !descriptor.capture || !descriptor.restore ||
        !descriptor.serialize || !descriptor.deserialize) {
        return fail("Component descriptor is missing required operations");
    }
    if (m_by_key.contains(descriptor.serialized_key)) {
        return fail("Duplicate component serialized key: " + descriptor.serialized_key);
    }
    if (m_by_type.contains(descriptor.type)) {
        return fail("Component runtime type is already registered");
    }

    const std::size_t index = m_descriptors.size();
    m_by_key.emplace(descriptor.serialized_key, index);
    m_by_type.emplace(descriptor.type, index);
    m_descriptors.push_back(std::move(descriptor));
    return true;
}

const ComponentDescriptor* ComponentRegistry::find_by_key(std::string_view key) const {
    auto found = m_by_key.find(std::string(key));
    return found == m_by_key.end() ? nullptr : &m_descriptors[found->second];
}

const ComponentDescriptor* ComponentRegistry::find_by_type(std::type_index type) const {
    auto found = m_by_type.find(type);
    return found == m_by_type.end() ? nullptr : &m_descriptors[found->second];
}

void ComponentRegistry::capture_entity(
    EntityManager& em,
    EntityID entity,
    ComponentSnapshotSet& snapshot
) const {
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (descriptor.has(em, entity)) descriptor.capture(em, entity, snapshot);
    }
}

void ComponentRegistry::restore_entity(
    EntityManager& em,
    EntityID entity,
    const ComponentSnapshotSet& snapshot
) const {
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        descriptor.restore(em, entity, snapshot);
    }
}

void ComponentRegistry::clone_entity_components(
    EntityManager& em,
    EntityID source,
    EntityID target
) const {
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (descriptor.has(em, source)) descriptor.clone(em, source, target);
    }
}

bool ComponentRegistry::add_default_component(
    std::string_view key,
    EntityManager& em,
    EntityID entity,
    std::string& error
) const {
    const ComponentDescriptor* descriptor = find_by_key(key);
    if (!descriptor) {
        error = "Unknown component key: " + std::string(key);
        return false;
    }
    if (!em.is_alive(entity)) {
        error = "Cannot add a component to an invalid entity";
        return false;
    }
    descriptor->add_default(em, entity);
    return true;
}

bool ComponentRegistry::remove_component(
    std::string_view key,
    EntityManager& em,
    EntityID entity,
    std::string& error
) const {
    const ComponentDescriptor* descriptor = find_by_key(key);
    if (!descriptor) {
        error = "Unknown component key: " + std::string(key);
        return false;
    }
    if (!descriptor->removable) {
        error = "Component cannot be removed: " + descriptor->serialized_key;
        return false;
    }
    if (!em.is_alive(entity)) {
        error = "Cannot remove a component from an invalid entity";
        return false;
    }
    descriptor->remove(em, entity);
    return true;
}

bool ComponentRegistry::serialize_entity(
    const EntityManager& em,
    EntityID entity,
    nlohmann::json& entity_json,
    const ComponentSerializationContext& context,
    std::string& error
) const {
    try {
        for (const ComponentDescriptor& descriptor : m_descriptors) {
            if (descriptor.has(em, entity)) {
                entity_json[descriptor.serialized_key] = descriptor.serialize(em, entity, context);
            } else if (descriptor.required) {
                error = "Entity " + std::to_string(entity) + " is missing required component '" +
                    descriptor.serialized_key + "'";
                return false;
            }
        }
    } catch (const std::exception& exception) {
        error = "Failed to serialize entity " + std::to_string(entity) + ": " + exception.what();
        return false;
    }
    return true;
}

bool ComponentRegistry::deserialize_entity(
    EntityManager& em,
    EntityID entity,
    const nlohmann::json& entity_json,
    const ComponentSerializationContext& context,
    std::string& error
) const {
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (!entity_json.contains(descriptor.serialized_key)) {
            if (descriptor.required) {
                error = "Entity " + std::to_string(entity) + " is missing required component '" +
                    descriptor.serialized_key + "'";
                return false;
            }
            continue;
        }
        try {
            if (!descriptor.deserialize(
                    em, entity, entity_json.at(descriptor.serialized_key), context, error
                )) {
                if (error.empty()) error = "Component deserializer rejected '" + descriptor.serialized_key + "'";
                return false;
            }
        } catch (const std::exception& exception) {
            error = "Failed to deserialize component '" + descriptor.serialized_key + "': " + exception.what();
            return false;
        }
    }
    return true;
}

std::vector<ComponentValidationIssue> ComponentRegistry::validate_entity(
    const EntityManager& em,
    EntityID entity
) const {
    std::vector<ComponentValidationIssue> issues;
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (!descriptor.has(em, entity)) {
            if (descriptor.required) {
                issues.push_back({descriptor.serialized_key, "Required component is missing"});
            }
            continue;
        }
        if (descriptor.validate) {
            if (auto issue = descriptor.validate(em, entity)) {
                issues.push_back({descriptor.serialized_key, std::move(*issue)});
            }
        }
    }
    return issues;
}

ComponentRegistry& default_component_registry() {
    static ComponentRegistry registry = [] {
        ComponentRegistry result;
        std::string error;

        const bool transform_registered = result.register_component<TransformComponent>({
            .display_name = "Transform",
            .serialized_key = "transform",
            .schema_version = 1,
            .required = true,
            .removable = false,
            .serialize = [](const TransformComponent& transform, const ComponentSerializationContext&) {
                return nlohmann::json{
                    {"position", {transform.position.x, transform.position.y, transform.position.z}},
                    {"scale", {transform.scale.x, transform.scale.y}},
                    {"rotation", transform.rotation},
                    {"pivot", {transform.pivot.x, transform.pivot.y}},
                    {"visible", transform.visible}
                };
            },
            .deserialize = [](
                TransformComponent& transform,
                const nlohmann::json& value,
                const ComponentSerializationContext&,
                std::string& deserialize_error
            ) {
                if (!value.is_object()) {
                    deserialize_error = "Transform must be an object";
                    return false;
                }
                const auto& position = value.at("position");
                const auto& scale = value.at("scale");
                const auto& pivot = value.at("pivot");
                if (!position.is_array() || position.size() != 3 ||
                    !scale.is_array() || scale.size() != 2 ||
                    !pivot.is_array() || pivot.size() != 2) {
                    deserialize_error = "Transform vectors have invalid dimensions";
                    return false;
                }
                transform.position = {position[0].get<float>(), position[1].get<float>(), position[2].get<float>()};
                transform.scale = {scale[0].get<float>(), scale[1].get<float>()};
                transform.rotation = value.at("rotation").get<float>();
                transform.pivot = {pivot[0].get<float>(), pivot[1].get<float>()};
                transform.visible = value.at("visible").get<bool>();
                transform.world_position = transform.position;
                transform.world_scale = transform.scale;
                transform.world_rotation = transform.rotation;
                transform.world_pivot = transform.pivot;
                transform.world_visible = transform.visible;
                return true;
            },
            .validate = [](const TransformComponent& transform) -> std::optional<std::string> {
                if (transform.scale.x == 0.0f || transform.scale.y == 0.0f) {
                    return "Scale cannot contain zero";
                }
                return std::nullopt;
            }
        }, &error);
        if (!transform_registered) throw std::runtime_error(error);

        const bool sprite_registered = result.register_component<SpriteComponent>({
            .display_name = "Sprite Renderer",
            .serialized_key = "sprite",
            .schema_version = 1,
            .required = false,
            .removable = true,
            .serialize = [](const SpriteComponent& sprite, const ComponentSerializationContext&) {
                nlohmann::json value{
                    {"texture", sprite.texture ? sprite.texture->get_filepath() : ""},
                    {"size", {sprite.size.x, sprite.size.y}},
                    {"color", {sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a}},
                    {"flip_x", sprite.flip_x},
                    {"flip_y", sprite.flip_y},
                    {"visible", sprite.visible},
                    {"sorting_layer", sprite.sorting_layer},
                    {"order_in_layer", sprite.order_in_layer},
                    {"has_outline", sprite.has_outline},
                    {"outline_color", {
                        sprite.outline_color.r,
                        sprite.outline_color.g,
                        sprite.outline_color.b,
                        sprite.outline_color.a
                    }},
                    {"outline_thickness", sprite.outline_thickness},
                    {"outline_pulse", sprite.outline_pulse},
                    {"use_src_rect", sprite.use_src_rect}
                };
                if (sprite.use_src_rect) {
                    value["src_rect"] = {
                        sprite.src_rect.x, sprite.src_rect.y,
                        sprite.src_rect.w, sprite.src_rect.h
                    };
                }
                return value;
            },
            .deserialize = [](
                SpriteComponent& sprite,
                const nlohmann::json& value,
                const ComponentSerializationContext& context,
                std::string& deserialize_error
            ) {
                if (!value.is_object()) {
                    deserialize_error = "Sprite Renderer must be an object";
                    return false;
                }
                const std::string texture_path = value.at("texture").get<std::string>();
                if (context.renderer && context.asset_manager && !texture_path.empty()) {
                    sprite.texture = context.asset_manager->get_texture(context.renderer, texture_path);
                }
                if (!sprite.texture && context.renderer && context.asset_manager) {
                    sprite.texture = context.asset_manager->get_white_texture(context.renderer);
                }
                const auto& size = value.at("size");
                const auto& color = value.at("color");
                const auto& outline_color = value.at("outline_color");
                if (!size.is_array() || size.size() != 2 ||
                    !color.is_array() || color.size() != 4 ||
                    !outline_color.is_array() || outline_color.size() != 4) {
                    deserialize_error = "Sprite vectors have invalid dimensions";
                    return false;
                }
                sprite.size = {size[0].get<float>(), size[1].get<float>()};
                sprite.color = {
                    color[0].get<std::uint8_t>(), color[1].get<std::uint8_t>(),
                    color[2].get<std::uint8_t>(), color[3].get<std::uint8_t>()
                };
                sprite.flip_x = value.at("flip_x").get<bool>();
                sprite.flip_y = value.at("flip_y").get<bool>();
                sprite.visible = value.at("visible").get<bool>();
                sprite.sorting_layer = value.at("sorting_layer").get<std::int16_t>();
                sprite.order_in_layer = value.at("order_in_layer").get<std::int16_t>();
                sprite.has_outline = value.at("has_outline").get<bool>();
                sprite.outline_color = {
                    outline_color[0].get<std::uint8_t>(), outline_color[1].get<std::uint8_t>(),
                    outline_color[2].get<std::uint8_t>(), outline_color[3].get<std::uint8_t>()
                };
                sprite.outline_thickness = value.at("outline_thickness").get<float>();
                sprite.outline_pulse = value.at("outline_pulse").get<bool>();
                sprite.use_src_rect = value.at("use_src_rect").get<bool>();
                if (sprite.use_src_rect) {
                    const auto& source = value.at("src_rect");
                    if (!source.is_array() || source.size() != 4) {
                        deserialize_error = "Sprite source rectangle has invalid dimensions";
                        return false;
                    }
                    sprite.src_rect = {
                        source[0].get<float>(), source[1].get<float>(),
                        source[2].get<float>(), source[3].get<float>()
                    };
                }
                return true;
            },
            .validate = [](const SpriteComponent& sprite) -> std::optional<std::string> {
                if (sprite.size.x <= 0.0f || sprite.size.y <= 0.0f) {
                    return "Size must be positive";
                }
                if (sprite.outline_thickness < 0.0f) {
                    return "Outline thickness cannot be negative";
                }
                return std::nullopt;
            }
        }, &error);
        if (!sprite_registered) throw std::runtime_error(error);
        return result;
    }();
    return registry;
}

} // namespace Engine

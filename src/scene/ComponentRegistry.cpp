#include "ComponentRegistry.hpp"
#include "BuiltinComponentRegistrations.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/AssetManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/material/MaterialAsset.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>

namespace Engine {
namespace {

bool read_schema_version(const nlohmann::json& value, std::uint32_t& version) {
    if (value.is_number_unsigned()) {
        version = value.get<std::uint32_t>();
        return true;
    }
    if (!value.is_number_integer()) return false;
    const auto signed_version = value.get<std::int64_t>();
    if (signed_version < 0 || signed_version > std::numeric_limits<std::uint32_t>::max()) return false;
    version = static_cast<std::uint32_t>(signed_version);
    return true;
}

} // namespace

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
    const EntityManager& em,
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

void ComponentRegistry::remap_entity_references(
    EntityManager& em,
    EntityID entity,
    const std::unordered_map<EntityID, EntityID>& old_to_new
) const {
    if (!em.is_alive(entity)) return;
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (descriptor.remap_references && descriptor.has(em, entity)) {
            descriptor.remap_references(em, entity, old_to_new);
        }
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
    const bool already_present = descriptor->has(em, entity);
    descriptor->add_default(em, entity);
    if (!already_present && descriptor->finalize) {
        const ComponentFinalizeContext context{
            .reason = ComponentFinalizeReason::DefaultCreation
        };
        if (!descriptor->finalize(em, entity, context, error)) {
            descriptor->remove(em, entity);
            return false;
        }
    }
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

bool ComponentRegistry::migrate_entity_components(
    nlohmann::json& entity_json,
    const nlohmann::json& component_versions,
    std::string& error
) const {
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (!entity_json.contains(descriptor.serialized_key)) continue;
        std::uint32_t version = 0;
        if (!component_versions.contains(descriptor.serialized_key) ||
            !read_schema_version(component_versions.at(descriptor.serialized_key), version)) {
            error = "Missing or invalid schema version for component '" + descriptor.serialized_key + "'";
            return false;
        }
        if (version > descriptor.schema_version) {
            error = "Component '" + descriptor.serialized_key + "' uses unsupported future schema version " +
                std::to_string(version);
            return false;
        }
        while (version < descriptor.schema_version) {
            if (!descriptor.migrate) {
                error = "No migration from schema version " + std::to_string(version) +
                    " is registered for component '" + descriptor.serialized_key + "'";
                return false;
            }
            if (!descriptor.migrate(version, entity_json.at(descriptor.serialized_key), error)) {
                if (error.empty()) {
                    error = "Migration failed for component '" + descriptor.serialized_key + "'";
                }
                return false;
            }
            ++version;
        }
    }
    return true;
}

bool ComponentRegistry::finalize_entity(
    EntityManager& em,
    EntityID entity,
    const ComponentFinalizeContext& context,
    std::string& error
) const {
    if (!em.is_alive(entity)) {
        error = "Cannot finalize components for an invalid entity";
        return false;
    }
    for (const ComponentDescriptor& descriptor : m_descriptors) {
        if (!descriptor.finalize || !descriptor.has(em, entity)) continue;
        try {
            if (!descriptor.finalize(em, entity, context, error)) {
                if (error.empty()) error = "Component finalizer rejected '" + descriptor.serialized_key + "'";
                return false;
            }
        } catch (const std::exception& exception) {
            error = "Failed to finalize component '" + descriptor.serialized_key + "': " + exception.what();
            return false;
        }
    }
    return true;
}

bool ComponentRegistry::finalize_entities(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const ComponentFinalizeContext& context,
    std::string& error
) const {
    for (const EntityID entity : entities) {
        if (!finalize_entity(em, entity, context, error)) return false;
    }
    return true;
}

void ComponentRegistry::reconcile_replacement(
    const EntityManager& previous,
    EntityManager& current
) const {
    for (const EntityID entity : previous.get_living_entities()) {
        for (const ComponentDescriptor& descriptor : m_descriptors) {
            if (!descriptor.after_remove || !descriptor.has(previous, entity)) continue;
            if (!current.is_alive(entity) || !descriptor.has(current, entity)) {
                descriptor.after_remove(current, entity);
            }
        }
    }
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
            .serialize = [](const SpriteComponent& sprite, const ComponentSerializationContext& context) {
                nlohmann::json value{
                    {"size", {sprite.size.x, sprite.size.y}},
                    {"color", {sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a}},
                    {"flip_x", sprite.flip_x},
                    {"flip_y", sprite.flip_y},
                    {"visible", sprite.visible},
                    {"sorting_layer", sprite.sorting_layer},
                    {"order_in_layer", sprite.order_in_layer},
                    {"render_layer", sprite.render_layer},
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
                AssetHandle texture_handle = sprite.texture_handle;
                if (!texture_handle.is_valid() && sprite.texture && !sprite.texture->get_filepath().empty()) {
                    if (!context.asset_database) {
                        throw std::runtime_error("Sprite texture serialization requires an AssetDatabase");
                    }
                    const AssetRecord* texture = context.asset_database->find_by_path(sprite.texture->get_filepath());
                    if (!texture || texture->status != AssetStatus::Ready || texture->type != AssetType::Texture) {
                        throw std::runtime_error("Sprite references an unregistered texture: " + sprite.texture->get_filepath());
                    }
                    texture_handle = texture->handle;
                }
                if (texture_handle.is_valid()) value["texture_handle"] = texture_handle.to_string();
                if (sprite.material_handle.is_valid()) value["material_handle"] = sprite.material_handle.to_string();
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
                sprite.texture_handle = {};
                if (value.contains("texture_handle")) {
                    if (!value["texture_handle"].is_string()) {
                        deserialize_error = "Sprite texture_handle must be a string";
                        return false;
                    }
                    const auto handle = AssetHandle::from_string(value["texture_handle"].get<std::string>());
                    const AssetRecord* texture = context.asset_database && handle ? context.asset_database->find(*handle) : nullptr;
                    if (handle) {
                        // The handle is authored data even when its runtime resource
                        // is unavailable. Keep it for diagnostics and later reload.
                        sprite.texture_handle = *handle;
                    }
                    if (texture && texture->status == AssetStatus::Ready && texture->type == AssetType::Texture) {
                        if (context.renderer && context.asset_manager) {
                            sprite.texture = context.asset_manager->get_texture(context.renderer, texture->source_path);
                        }
                    }
                }
                sprite.material.reset();
                sprite.material_handle = {};
                if (value.contains("material_handle")) {
                    if (!value["material_handle"].is_string()) { deserialize_error = "Sprite material_handle must be a string"; return false; }
                    const auto handle = AssetHandle::from_string(value["material_handle"].get<std::string>());
                    if (handle && context.asset_database) {
                        sprite.material_handle = *handle;
                        sprite.material = MaterialAsset::load(*handle, *context.asset_database);
                    }
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
                sprite.render_layer = value.value("render_layer", DEFAULT_RENDER_LAYER);
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
            .finalize = [](
                EntityManager&,
                EntityID,
                SpriteComponent& sprite,
                const ComponentFinalizeContext& context,
                std::string&
            ) {
                if (!sprite.texture && context.serialization.renderer &&
                    context.serialization.asset_manager) {
                    sprite.texture = context.serialization.asset_manager->get_white_texture(
                        context.serialization.renderer
                    );
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
        register_additional_builtin_components(result);
        return result;
    }();
    return registry;
}

} // namespace Engine

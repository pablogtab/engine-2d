#include "SceneSchema.hpp"

#include "ComponentRegistry.hpp"
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace Engine {
namespace {

using Json = nlohmann::json;

bool read_schema_version(const Json& value, std::uint32_t& version) {
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

bool migrate_v1_to_v2(Json& root, const ComponentRegistry& registry, std::string&) {
    Json historical_versions = Json::object();
    for (const ComponentDescriptor& descriptor : registry.descriptors()) {
        historical_versions[descriptor.serialized_key] = static_cast<std::uint32_t>(1);
    }
    root["component_versions"] = std::move(historical_versions);
    root["version"] = static_cast<std::uint32_t>(2);
    return true;
}

bool migrate_scene_once(
    std::uint32_t version,
    Json& root,
    const ComponentRegistry& registry,
    std::string& error
) {
    switch (version) {
        case 1: return migrate_v1_to_v2(root, registry, error);
        default:
            error = "No scene migration is registered from version " + std::to_string(version);
            return false;
    }
}

} // namespace

nlohmann::json SceneSchema::component_versions(const ComponentRegistry& registry) {
    Json versions = Json::object();
    for (const ComponentDescriptor& descriptor : registry.descriptors()) {
        versions[descriptor.serialized_key] = descriptor.schema_version;
    }
    return versions;
}

bool SceneSchema::migrate_to_current(
    nlohmann::json& root,
    const ComponentRegistry& registry,
    std::string& error
) {
    if (!root.is_object()) {
        error = "Scene root must be an object";
        return false;
    }
    std::uint32_t version = 0;
    if (!root.contains("version") || !read_schema_version(root.at("version"), version)) {
        error = "Scene is missing a valid unsigned schema version";
        return false;
    }
    if (version == 0) {
        error = "Scene schema version must be greater than zero";
        return false;
    }
    if (version > CURRENT_VERSION) {
        error = "Scene uses unsupported future schema version " + std::to_string(version) +
            "; current version is " + std::to_string(CURRENT_VERSION);
        return false;
    }
    while (version < CURRENT_VERSION) {
        if (!migrate_scene_once(version, root, registry, error)) return false;
        std::uint32_t migrated_version = 0;
        if (!read_schema_version(root.at("version"), migrated_version)) {
            error = "Scene migration produced an invalid schema version";
            return false;
        }
        if (migrated_version <= version) {
            error = "Scene migration did not advance beyond version " + std::to_string(version);
            return false;
        }
        version = migrated_version;
    }

    if (!root.contains("component_versions") || !root.at("component_versions").is_object()) {
        error = "Scene is missing the component_versions object";
        return false;
    }
    if (!root.contains("entities") || !root.at("entities").is_array()) {
        error = "Scene entities must be an array";
        return false;
    }

    const Json versions = root.at("component_versions");
    for (Json& entity : root.at("entities")) {
        if (!entity.is_object()) {
            error = "Every scene entity must be an object";
            return false;
        }
        if (!registry.migrate_entity_components(entity, versions, error)) return false;
    }
    root["component_versions"] = component_versions(registry);
    return true;
}

bool SceneSchema::validate_structure(
    const nlohmann::json& root,
    const ComponentRegistry& registry,
    std::string& error
) {
    std::uint32_t scene_version = 0;
    if (!root.is_object() || !root.contains("version") ||
        !read_schema_version(root.at("version"), scene_version) ||
        scene_version != CURRENT_VERSION) {
        error = "Scene was not migrated to the current schema version";
        return false;
    }
    if (!root.contains("scene_name") || !root.at("scene_name").is_string()) {
        error = "Scene name must be a string";
        return false;
    }
    if (!root.contains("entities") || !root.at("entities").is_array()) {
        error = "Scene entities must be an array";
        return false;
    }
    if (!root.contains("component_versions") || !root.at("component_versions").is_object()) {
        error = "Scene is missing the component_versions object";
        return false;
    }
    const auto& versions = root.at("component_versions");
    for (const ComponentDescriptor& descriptor : registry.descriptors()) {
        std::uint32_t component_version = 0;
        if (!versions.contains(descriptor.serialized_key) ||
            !read_schema_version(versions.at(descriptor.serialized_key), component_version) ||
            component_version != descriptor.schema_version) {
            error = "Component schema version mismatch for '" + descriptor.serialized_key + "'";
            return false;
        }
    }

    std::unordered_set<EntityID> ids;
    std::unordered_map<EntityID, EntityID> parents;
    std::unordered_set<std::string> allowed_keys{"id", "hierarchy"};
    for (const ComponentDescriptor& descriptor : registry.descriptors()) {
        allowed_keys.insert(descriptor.serialized_key);
    }

    for (const auto& entity : root.at("entities")) {
        std::uint32_t serialized_id = 0;
        if (!entity.is_object() || !entity.contains("id") ||
            !read_schema_version(entity.at("id"), serialized_id)) {
            error = "Every entity must have an unsigned ID";
            return false;
        }
        const EntityID id = static_cast<EntityID>(serialized_id);
        if (id == 0 || !ids.insert(id).second) {
            error = "Entity IDs must be non-zero and unique; invalid ID " + std::to_string(id);
            return false;
        }
        for (const auto& [key, value] : entity.items()) {
            (void)value;
            if (!allowed_keys.contains(key)) {
                error = "Entity " + std::to_string(id) + " contains unknown component or field '" + key + "'";
                return false;
            }
        }
        if (entity.contains("hierarchy")) {
            const auto& hierarchy = entity.at("hierarchy");
            std::uint32_t serialized_parent = 0;
            if (!hierarchy.is_object() || !hierarchy.contains("parent") ||
                !read_schema_version(hierarchy.at("parent"), serialized_parent)) {
                error = "Entity " + std::to_string(id) + " has an invalid hierarchy record";
                return false;
            }
            parents[id] = static_cast<EntityID>(serialized_parent);
        }
    }

    for (const auto& [child, parent] : parents) {
        if (parent != 0 && !ids.contains(parent)) {
            error = "Entity " + std::to_string(child) + " references missing parent " + std::to_string(parent);
            return false;
        }
    }

    std::unordered_map<EntityID, std::uint8_t> visit_state;
    std::function<bool(EntityID)> visit = [&](EntityID entity) {
        const std::uint8_t state = visit_state[entity];
        if (state == 2) return true;
        if (state == 1) {
            error = "Hierarchy cycle detected at entity " + std::to_string(entity);
            return false;
        }
        visit_state[entity] = 1;
        const auto parent = parents.find(entity);
        if (parent != parents.end() && parent->second != 0 && !visit(parent->second)) return false;
        visit_state[entity] = 2;
        return true;
    };
    for (const EntityID entity : ids) {
        if (!visit(entity)) return false;
    }
    return true;
}

} // namespace Engine

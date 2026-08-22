#include "EditorAiAuthoring.hpp"

#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Engine {
namespace {

using Json = nlohmann::json;

constexpr const char* CREATE_ENTITY_TOOL = "propose_create_entity";
constexpr const char* SET_TRANSFORM_TOOL = "propose_set_transform";
constexpr const char* RENAME_ENTITY_TOOL = "propose_rename_entity";
constexpr const char* ADD_COMPONENT_TOOL = "propose_add_component";
constexpr const char* REMOVE_COMPONENT_TOOL = "propose_remove_component";
constexpr const char* CONFIGURE_COMPONENT_TOOL = "propose_configure_component";
constexpr const char* REPARENT_ENTITY_TOOL = "propose_reparent_entity";
constexpr const char* DELETE_ENTITY_TOOL = "propose_delete_entity";
constexpr const char* DUPLICATE_ENTITIES_TOOL = "propose_duplicate_entities";
constexpr const char* DELETE_ENTITIES_TOOL = "propose_delete_entities";
constexpr const char* INSTANTIATE_PREFAB_TOOL = "propose_instantiate_prefab";
constexpr const char* UNPACK_PREFAB_TOOL = "propose_unpack_prefab";
constexpr const char* IMPORT_GENERATED_TEXTURE_TOOL = "propose_import_generated_texture";

struct EntityTarget {
    EntityID entity_id{0};
    std::string alias;
};

struct AliasState {
    std::unordered_map<std::string, EntityID> entities;
};

Json target_properties() {
    return {
        {"entity_id", {{"type", Json::array({"integer", "null"})}, {"minimum", 1}}},
        {"entity_alias", {{"type", Json::array({"string", "null"})}}}
    };
}

Json nullable_schema(Json schema) {
    schema["type"] = Json::array({schema.at("type"), "null"});
    return schema;
}

bool valid_alias(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_' || character == '-';
    });
}

bool valid_name(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 32;
    });
}

bool only_keys(const Json& value, std::initializer_list<const char*> keys) {
    if (!value.is_object()) return false;
    std::unordered_set<std::string> allowed;
    for (const char* key : keys) allowed.insert(key);
    for (auto item = value.begin(); item != value.end(); ++item) {
        if (!allowed.contains(item.key())) return false;
    }
    return true;
}

bool finite_number(const Json& value, double limit = 1'000'000.0) {
    if (!value.is_number()) return false;
    const double number = value.get<double>();
    return std::isfinite(number) && std::abs(number) <= limit;
}

bool finite_vector(const Json& value, std::size_t size, double limit = 1'000'000.0) {
    return value.is_array() && value.size() == size &&
        std::all_of(value.begin(), value.end(), [limit](const Json& item) {
            return finite_number(item, limit);
        });
}

bool finite_vector_range(
    const Json& value,
    std::size_t minimum_size,
    std::size_t maximum_size,
    double limit = 1'000'000.0
) {
    return value.is_array() && value.size() >= minimum_size && value.size() <= maximum_size &&
        std::all_of(value.begin(), value.end(), [limit](const Json& item) {
            return finite_number(item, limit);
        });
}

std::optional<EntityTarget> read_target(const Json& arguments, std::string& error) {
    const bool has_id = arguments.contains("entity_id") && !arguments.at("entity_id").is_null();
    const bool has_alias = arguments.contains("entity_alias") && !arguments.at("entity_alias").is_null();
    if (has_id == has_alias) {
        error = "Exactly one of entity_id or entity_alias is required";
        return std::nullopt;
    }
    EntityTarget target;
    if (has_id) {
        if (!arguments.at("entity_id").is_number_unsigned() &&
            !arguments.at("entity_id").is_number_integer()) {
            error = "entity_id must be a positive integer";
            return std::nullopt;
        }
        const auto value = arguments.at("entity_id").get<std::int64_t>();
        if (value <= 0 || value > std::numeric_limits<EntityID>::max()) {
            error = "entity_id is outside the supported range";
            return std::nullopt;
        }
        target.entity_id = static_cast<EntityID>(value);
    } else {
        if (!arguments.at("entity_alias").is_string()) {
            error = "entity_alias must be a string";
            return std::nullopt;
        }
        target.alias = arguments.at("entity_alias").get<std::string>();
        if (!valid_alias(target.alias)) {
            error = "entity_alias may contain only letters, numbers, underscores, and hyphens";
            return std::nullopt;
        }
    }
    return target;
}

EntityID resolve_target(
    const EntityTarget& target,
    const std::shared_ptr<AliasState>& aliases
) {
    if (target.entity_id != 0) return target.entity_id;
    const auto found = aliases->entities.find(target.alias);
    return found == aliases->entities.end() ? 0 : found->second;
}

std::string target_label(const EntityTarget& target) {
    return target.entity_id != 0
        ? "entity #" + std::to_string(target.entity_id)
        : "@" + target.alias;
}

std::vector<std::string> present_component_keys(
    const ComponentRegistry& registry,
    const EntityManager& entities,
    EntityID entity
) {
    std::vector<std::string> keys;
    for (const ComponentDescriptor& descriptor : registry.descriptors()) {
        if (descriptor.has(entities, entity)) keys.push_back(descriptor.serialized_key);
    }
    return keys;
}

class DeferredEditorCommand final : public IEditorCommand {
public:
    using Factory = std::function<std::unique_ptr<IEditorCommand>()>;

    DeferredEditorCommand(Factory factory, std::string description)
        : m_factory(std::move(factory)), m_description(std::move(description)) {}

    bool execute() override {
        if (m_command || !m_factory) return false;
        m_command = m_factory();
        return m_command && m_command->execute();
    }
    bool undo() override { return m_command && m_command->undo(); }
    bool redo() override { return m_command && m_command->redo(); }
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    Factory m_factory;
    std::unique_ptr<IEditorCommand> m_command;
    std::string m_description;
};

class InstantiateAiPrefabCommand final : public IEditorCommand {
public:
    InstantiateAiPrefabCommand(
        EntityManager* entities,
        std::shared_ptr<EditorSelection> selection,
        const EditorEntityStateStore* state_store,
        SDL_Renderer* renderer,
        std::shared_ptr<AliasState> aliases,
        std::string alias,
        std::string prefab_path,
        EntityTarget parent,
        std::optional<TransformComponent> transform
    ) : m_entities(entities), m_selection(std::move(selection)), m_state_store(state_store),
        m_renderer(renderer), m_aliases(std::move(aliases)), m_alias(std::move(alias)),
        m_prefab_path(std::move(prefab_path)), m_parent(std::move(parent)),
        m_transform(std::move(transform)) {}

    bool execute() override {
        if (!m_entities || !m_aliases) return false;
        if (m_command) return redo();
        const EntityID parent = resolve_target(m_parent, m_aliases);
        if ((m_parent.entity_id != 0 || !m_parent.alias.empty()) &&
            (parent == 0 || !m_entities->is_alive(parent))) return false;
        auto command = std::make_unique<InstantiatePrefabCommand>(
            m_entities, m_selection, m_prefab_path, false, m_transform, parent,
            m_renderer, m_state_store
        );
        if (!command->is_valid() || !command->execute() || command->get_root_entity() == 0) return false;
        m_root = command->get_root_entity();
        m_aliases->entities[m_alias] = m_root;
        m_command = std::move(command);
        return true;
    }

    bool undo() override {
        if (!m_command || !m_command->undo()) return false;
        m_aliases->entities.erase(m_alias);
        return true;
    }

    bool redo() override {
        if (!m_command || !m_command->redo()) return false;
        m_root = m_command->get_root_entity();
        m_aliases->entities[m_alias] = m_root;
        return true;
    }

    [[nodiscard]] std::string get_description() const override {
        return "Instantiate prefab as @" + m_alias;
    }

private:
    EntityManager* m_entities{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    const EditorEntityStateStore* m_state_store{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    std::shared_ptr<AliasState> m_aliases;
    std::string m_alias;
    std::string m_prefab_path;
    EntityTarget m_parent;
    std::optional<TransformComponent> m_transform;
    std::unique_ptr<InstantiatePrefabCommand> m_command;
    EntityID m_root{0};
};

class CreateAiEntityCommand final : public IEditorCommand {
public:
    CreateAiEntityCommand(
        EntityManager* entities,
        ComponentRegistry* registry,
        std::shared_ptr<EditorSelection> selection,
        std::shared_ptr<EditorEntityStateStore> state_store,
        ComponentSerializationContext serialization,
        std::shared_ptr<AliasState> aliases,
        std::string alias,
        std::string name,
        TransformComponent transform,
        EntityTarget parent,
        std::vector<std::string> components
    ) : m_entities(entities), m_registry(registry), m_selection(std::move(selection)),
        m_state_store(std::move(state_store)), m_serialization(serialization),
        m_aliases(std::move(aliases)), m_alias(std::move(alias)), m_name(std::move(name)),
        m_transform(transform), m_parent(std::move(parent)), m_components(std::move(components)) {}

    bool execute() override {
        if (!m_entities || !m_registry || m_created_id != 0) return false;
        m_selection_before = m_selection ? m_selection->get_selected() : std::unordered_set<EntityID>{};

        const EntityID parent = resolve_target(m_parent, m_aliases);
        if ((!m_parent.alias.empty() || m_parent.entity_id != 0) &&
            (parent == 0 || !m_entities->is_alive(parent))) return false;
        if (parent != 0 && m_state_store && m_state_store->is_locked(parent)) return false;

        const EntityID entity = m_entities->create_entity();
        if (entity == 0) return false;
        m_entities->add_component<TagComponent>(entity, TagComponent{m_name});
        m_entities->add_component<TransformComponent>(entity, m_transform);
        m_entities->add_component<HierarchyComponent>(entity, HierarchyComponent{});

        for (const std::string& key : m_components) {
            const ComponentDescriptor* descriptor = m_registry->find_by_key(key);
            if (!descriptor || descriptor->has(*m_entities, entity)) {
                cleanup(entity);
                return false;
            }
            descriptor->add_default(*m_entities, entity);
            if (descriptor->finalize) {
                std::string error;
                if (!descriptor->finalize(
                        *m_entities,
                        entity,
                        ComponentFinalizeContext{
                            .serialization = m_serialization,
                            .reason = ComponentFinalizeReason::DefaultCreation
                        },
                        error
                    )) {
                    cleanup(entity);
                    return false;
                }
            }
        }
        if (parent != 0 && !m_entities->set_parent(entity, parent).succeeded()) {
            cleanup(entity);
            return false;
        }
        if (!m_registry->validate_entity(*m_entities, entity).empty()) {
            cleanup(entity);
            return false;
        }

        m_created_id = entity;
        m_snapshot = capture_entity_snapshot(*m_entities, entity);
        m_aliases->entities[m_alias] = entity;
        if (m_state_store) m_state_store->set_component_order(entity, m_components);
        if (m_selection) m_selection->select(entity, false);
        return true;
    }

    bool undo() override {
        if (!m_entities || m_created_id == 0 || !m_entities->is_alive(m_created_id)) return false;
        cleanup(m_created_id);
        m_aliases->entities.erase(m_alias);
        if (m_selection) m_selection->set_selection(m_selection_before);
        return true;
    }

    bool redo() override {
        if (!m_entities || m_created_id == 0 || m_entities->is_alive(m_created_id)) return false;
        if (!restore_entity_snapshot(*m_entities, m_snapshot)) return false;
        if (m_snapshot.has_hierarchy && m_snapshot.hierarchy.parent != 0) {
            if (!m_entities->is_alive(m_snapshot.hierarchy.parent) ||
                !m_entities->set_parent(m_created_id, m_snapshot.hierarchy.parent).succeeded()) {
                cleanup(m_created_id);
                return false;
            }
        }
        std::string error;
        if (!m_registry->finalize_entity(
                *m_entities,
                m_created_id,
                ComponentFinalizeContext{
                    .serialization = m_serialization,
                    .reason = ComponentFinalizeReason::SnapshotRestore
                },
                error
            )) {
            cleanup(m_created_id);
            return false;
        }
        m_aliases->entities[m_alias] = m_created_id;
        if (m_state_store) m_state_store->set_component_order(m_created_id, m_components);
        if (m_selection) m_selection->select(m_created_id, false);
        return true;
    }

    [[nodiscard]] std::string get_description() const override { return "Create " + m_name; }

private:
    void cleanup(EntityID entity) {
        if (!m_entities || !m_entities->is_alive(entity)) return;
        if (m_entities->has_component<TilemapComponent>(entity)) {
            TilemapColliderSystem::clear_composite_colliders(*m_entities, entity);
        }
        m_entities->destroy_entity(entity);
        if (m_state_store) m_state_store->remove(entity);
    }

    EntityManager* m_entities{nullptr};
    ComponentRegistry* m_registry{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    std::shared_ptr<EditorEntityStateStore> m_state_store;
    ComponentSerializationContext m_serialization;
    std::shared_ptr<AliasState> m_aliases;
    std::string m_alias;
    std::string m_name;
    TransformComponent m_transform;
    EntityTarget m_parent;
    std::vector<std::string> m_components;
    EntityID m_created_id{0};
    EntitySnapshot m_snapshot;
    std::unordered_set<EntityID> m_selection_before;
};

class SetAiComponentPresenceCommand final : public IEditorCommand {
public:
    SetAiComponentPresenceCommand(
        EntityManager* entities,
        ComponentRegistry* registry,
        std::shared_ptr<EditorEntityStateStore> state_store,
        ComponentSerializationContext serialization,
        std::shared_ptr<AliasState> aliases,
        EntityTarget target,
        std::string component_key,
        bool present
    ) : m_entities(entities), m_registry(registry), m_state_store(std::move(state_store)),
        m_serialization(serialization), m_aliases(std::move(aliases)), m_target(std::move(target)),
        m_component_key(std::move(component_key)), m_present(present) {}

    bool execute() override {
        if (m_command || !m_entities || !m_registry) return false;
        const EntityID entity = resolve_target(m_target, m_aliases);
        if (entity == 0 || !m_entities->is_alive(entity) ||
            (m_state_store && m_state_store->is_locked(entity))) return false;
        const ComponentDescriptor* descriptor = m_registry->find_by_key(m_component_key);
        if (!descriptor || descriptor->has(*m_entities, entity) == m_present ||
            (!m_present && !descriptor->removable)) return false;

        ComponentSnapshotSet before;
        descriptor->capture(*m_entities, entity, before);
        const std::vector<std::string> present_before = present_component_keys(
            *m_registry, *m_entities, entity
        );
        const std::vector<std::string> order_before = m_state_store
            ? m_state_store->resolve_component_order(entity, present_before)
            : present_before;

        if (m_present) {
            descriptor->add_default(*m_entities, entity);
            if (descriptor->finalize) {
                std::string error;
                if (!descriptor->finalize(
                        *m_entities,
                        entity,
                        ComponentFinalizeContext{
                            .serialization = m_serialization,
                            .reason = ComponentFinalizeReason::DefaultCreation
                        },
                        error
                    )) {
                    descriptor->restore(*m_entities, entity, before);
                    return false;
                }
            }
        } else {
            descriptor->remove(*m_entities, entity);
        }
        if (!m_registry->validate_entity(*m_entities, entity).empty()) {
            descriptor->restore(*m_entities, entity, before);
            return false;
        }

        ComponentSnapshotSet after;
        descriptor->capture(*m_entities, entity, after);
        std::vector<std::string> order_after = order_before;
        std::erase(order_after, m_component_key);
        if (m_present) order_after.push_back(m_component_key);
        if (m_state_store) m_state_store->set_component_order(entity, order_after);

        m_command = std::make_unique<SetRegisteredComponentCommand>(
            m_registry,
            m_entities,
            entity,
            m_component_key,
            std::move(before),
            std::move(after),
            m_serialization,
            std::string(m_present ? "Add " : "Remove ") + descriptor->display_name,
            m_state_store,
            order_before,
            order_after
        );
        return true;
    }

    bool undo() override { return m_command && m_command->undo(); }
    bool redo() override { return m_command && m_command->redo(); }
    [[nodiscard]] std::string get_description() const override {
        return std::string(m_present ? "Add " : "Remove ") + m_component_key;
    }

private:
    EntityManager* m_entities{nullptr};
    ComponentRegistry* m_registry{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_state_store;
    ComponentSerializationContext m_serialization;
    std::shared_ptr<AliasState> m_aliases;
    EntityTarget m_target;
    std::string m_component_key;
    bool m_present{false};
    std::unique_ptr<SetRegisteredComponentCommand> m_command;
};

class PatchAiComponentCommand final : public IEditorCommand {
public:
    PatchAiComponentCommand(
        EntityManager* entities,
        ComponentRegistry* registry,
        std::shared_ptr<EditorEntityStateStore> state_store,
        ComponentSerializationContext serialization,
        std::shared_ptr<AliasState> aliases,
        EntityTarget target,
        std::string component_key,
        Json patch
    ) : m_entities(entities), m_registry(registry), m_state_store(std::move(state_store)),
        m_serialization(serialization), m_aliases(std::move(aliases)), m_target(std::move(target)),
        m_component_key(std::move(component_key)), m_patch(std::move(patch)) {}

    bool execute() override {
        if (m_command || !m_entities || !m_registry) return false;
        const EntityID entity = resolve_target(m_target, m_aliases);
        const auto fail = [&](const std::string& reason) {
            ENGINE_LOG_WARN(
                "[AI AUTHORING] Configure component rejected: entity={} component={} reason={}",
                entity,
                m_component_key,
                reason
            );
            return false;
        };
        if (entity == 0 || !m_entities->is_alive(entity)) return fail("entity is unavailable");
        if (m_state_store && m_state_store->is_locked(entity)) return fail("entity is locked");
        const ComponentDescriptor* descriptor = m_registry->find_by_key(m_component_key);
        if (!descriptor || !descriptor->has(*m_entities, entity) ||
            !descriptor->serialize || !descriptor->deserialize) {
            return fail("component is missing or not configurable");
        }

        ComponentSnapshotSet before;
        descriptor->capture(*m_entities, entity, before);
        const std::vector<std::string> present = present_component_keys(
            *m_registry, *m_entities, entity
        );
        const std::vector<std::string> order = m_state_store
            ? m_state_store->resolve_component_order(entity, present)
            : present;

        const auto restore_before = [&]() {
            descriptor->restore(*m_entities, entity, before);
            if (descriptor->finalize && descriptor->has(*m_entities, entity)) {
                std::string ignored;
                descriptor->finalize(
                    *m_entities,
                    entity,
                    ComponentFinalizeContext{
                        .serialization = m_serialization,
                        .reason = ComponentFinalizeReason::SnapshotRestore
                    },
                    ignored
                );
                if (!ignored.empty()) {
                    ENGINE_LOG_WARN(
                        "[AI AUTHORING] Component rollback finalization reported: entity={} component={} reason={}",
                        entity,
                        m_component_key,
                        ignored
                    );
                }
            }
        };

        try {
            Json data = descriptor->serialize(*m_entities, entity, m_serialization);
            if (!data.is_object()) return fail("serialized component data is not an object");
            data.merge_patch(m_patch);
            std::string error;
            if (!descriptor->deserialize(*m_entities, entity, data, m_serialization, error)) {
                restore_before();
                return fail(error.empty() ? "component deserializer rejected the patch" : error);
            }
            if (descriptor->finalize && !descriptor->finalize(
                    *m_entities,
                    entity,
                    ComponentFinalizeContext{
                        .serialization = m_serialization,
                        .reason = ComponentFinalizeReason::EditorComponentChange
                    },
                    error
                )) {
                restore_before();
                return fail(error.empty() ? "component finalizer rejected the patch" : error);
            }
            const std::vector<ComponentValidationIssue> issues =
                m_registry->validate_entity(*m_entities, entity);
            if (!issues.empty()) {
                restore_before();
                return fail(issues.front().component_key + ": " + issues.front().message);
            }
        } catch (const std::exception& exception) {
            restore_before();
            return fail(exception.what());
        }

        ComponentSnapshotSet after;
        descriptor->capture(*m_entities, entity, after);
        m_command = std::make_unique<SetRegisteredComponentCommand>(
            m_registry,
            m_entities,
            entity,
            m_component_key,
            std::move(before),
            std::move(after),
            m_serialization,
            "Configure " + descriptor->display_name,
            m_state_store,
            order,
            order
        );
        return true;
    }

    bool undo() override { return m_command && m_command->undo(); }
    bool redo() override { return m_command && m_command->redo(); }
    [[nodiscard]] std::string get_description() const override {
        return "Configure " + m_component_key;
    }

private:
    EntityManager* m_entities{nullptr};
    ComponentRegistry* m_registry{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_state_store;
    ComponentSerializationContext m_serialization;
    std::shared_ptr<AliasState> m_aliases;
    EntityTarget m_target;
    std::string m_component_key;
    Json m_patch;
    std::unique_ptr<SetRegisteredComponentCommand> m_command;
};

TransformComponent transform_from_create_arguments(const Json& arguments) {
    TransformComponent transform;
    if (arguments.contains("position")) {
        const auto& value = arguments.at("position");
        transform.position = {
            value[0].get<float>(),
            value[1].get<float>(),
            value.size() == 3 ? value[2].get<float>() : 0.0f
        };
    }
    if (arguments.contains("scale")) {
        const auto& value = arguments.at("scale");
        transform.scale = {value[0].get<float>(), value[1].get<float>()};
    }
    if (arguments.contains("pivot")) {
        const auto& value = arguments.at("pivot");
        transform.pivot = {value[0].get<float>(), value[1].get<float>()};
    }
    if (arguments.contains("rotation")) transform.rotation = arguments.at("rotation").get<float>();
    if (arguments.contains("visible")) transform.visible = arguments.at("visible").get<bool>();
    transform.world_position = transform.position;
    transform.world_scale = transform.scale;
    transform.world_pivot = transform.pivot;
    transform.world_rotation = transform.rotation;
    transform.world_visible = transform.visible;
    return transform;
}

void apply_transform_arguments(TransformComponent& transform, const Json& arguments) {
    if (arguments.contains("position")) {
        const auto& value = arguments.at("position");
        transform.position = {
            value[0].get<float>(),
            value[1].get<float>(),
            value.size() == 3 ? value[2].get<float>() : transform.position.z
        };
    }
    if (arguments.contains("scale")) {
        const auto& value = arguments.at("scale");
        transform.scale = {value[0].get<float>(), value[1].get<float>()};
    }
    if (arguments.contains("pivot")) {
        const auto& value = arguments.at("pivot");
        transform.pivot = {value[0].get<float>(), value[1].get<float>()};
    }
    if (arguments.contains("rotation")) transform.rotation = arguments.at("rotation").get<float>();
    if (arguments.contains("visible")) transform.visible = arguments.at("visible").get<bool>();
}

std::vector<std::string> component_list(const Json& arguments) {
    std::vector<std::string> result;
    if (!arguments.contains("components")) return result;
    for (const Json& item : arguments.at("components")) result.push_back(item.get<std::string>());
    return result;
}

std::optional<std::string> expected_asset_type(
    const std::string& component_key,
    const std::string& field
) {
    if (field == "texture_handle") return "texture";
    if (field == "font_handle") return "font";
    if (field == "material_handle") return "material";
    if (field == "profile_handle" && component_key == "render_effect") {
        return "render_layer_effect_profile";
    }
    return std::nullopt;
}

void collect_asset_handles(
    const Json& value,
    std::vector<std::pair<std::string, std::string>>& handles
) {
    if (value.is_object()) {
        for (auto item = value.begin(); item != value.end(); ++item) {
            if ((item.key().ends_with("_handle") || item.key() == "asset_handle") &&
                item.value().is_string() && !item.value().get<std::string>().empty()) {
                handles.emplace_back(item.key(), item.value().get<std::string>());
            }
            collect_asset_handles(item.value(), handles);
        }
    } else if (value.is_array()) {
        for (const Json& item : value) collect_asset_handles(item, handles);
    }
}

std::vector<EntityID> read_entity_id_list(const Json& arguments, std::string& error) {
    if (!only_keys(arguments, {"entity_ids"}) || !arguments.contains("entity_ids") ||
        !arguments.at("entity_ids").is_array() || arguments.at("entity_ids").empty() ||
        arguments.at("entity_ids").size() > 20) {
        error = "entity_ids must be an array containing 1 to 20 entity IDs";
        return {};
    }
    std::vector<EntityID> result;
    std::unordered_set<EntityID> unique;
    for (const Json& item : arguments.at("entity_ids")) {
        if (!item.is_number_integer()) {
            error = "entity_ids must contain positive integers";
            return {};
        }
        const std::int64_t value = item.get<std::int64_t>();
        if (value <= 0 || value > std::numeric_limits<EntityID>::max() ||
            !unique.insert(static_cast<EntityID>(value)).second) {
            error = "entity_ids contains an invalid or duplicate entity ID";
            return {};
        }
        result.push_back(static_cast<EntityID>(value));
    }
    return result;
}

bool valid_generated_texture_destination(const std::string& path_text) {
    if (path_text.empty() || path_text.size() > 240 || path_text.find('\\') != std::string::npos) return false;
    const std::filesystem::path path(path_text);
    if (path.is_absolute() || path.extension() != ".png") return false;
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || *normalized.begin() != "Assets") return false;
    return std::none_of(normalized.begin(), normalized.end(), [](const auto& part) {
        return part == ".." || part == ".";
    });
}

} // namespace

EditorAiAuthoringRegistry::EditorAiAuthoringRegistry(ComponentRegistry* component_registry)
    : m_component_registry(component_registry ? component_registry : &default_component_registry()) {
    for (const ComponentDescriptor& descriptor : m_component_registry->descriptors()) {
        if (descriptor.serialized_key == "transform" || descriptor.serialized_key == "tag" ||
            descriptor.serialized_key == "prefab_link") continue;
        m_addable_component_keys.push_back(descriptor.serialized_key);
        if (descriptor.removable) m_removable_component_keys.push_back(descriptor.serialized_key);
        if (descriptor.serialize && descriptor.deserialize) {
            m_configurable_component_keys.push_back(descriptor.serialized_key);
        }
    }
    std::sort(m_addable_component_keys.begin(), m_addable_component_keys.end());
    std::sort(m_removable_component_keys.begin(), m_removable_component_keys.end());
    std::sort(m_configurable_component_keys.begin(), m_configurable_component_keys.end());

    const Json vector2_schema{{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}};
    const Json position_schema{{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 3}};
    const Json addable_schema{{"type", "string"}, {"enum", m_addable_component_keys}};
    const Json removable_schema{{"type", "string"}, {"enum", m_removable_component_keys}};
    const Json configurable_schema{{"type", "string"}, {"enum", m_configurable_component_keys}};
    const Json entity_ids_schema{
        {"type", "array"},
        {"items", {{"type", "integer"}, {"minimum", 1}}},
        {"minItems", 1},
        {"maxItems", 20}
    };

    Json transform_properties = target_properties();
    transform_properties.update({
        {"position", nullable_schema(position_schema)},
        {"scale", nullable_schema(vector2_schema)},
        {"pivot", nullable_schema(vector2_schema)},
        {"rotation", nullable_schema({{"type", "number"}})},
        {"visible", nullable_schema({{"type", "boolean"}})}
    });
    Json name_properties = target_properties();
    name_properties["name"] = {{"type", "string"}};
    Json component_properties = target_properties();
    component_properties["component_key"] = addable_schema;
    Json removable_properties = target_properties();
    removable_properties["component_key"] = removable_schema;
    Json configurable_properties = target_properties();
    configurable_properties["component_key"] = configurable_schema;
    configurable_properties["patch_json"] = {{"type", "string"}};
    Json reparent_properties = target_properties();
    reparent_properties.update({
        {"parent_entity_id", {{"type", Json::array({"integer", "null"})}, {"minimum", 0}}},
        {"parent_entity_alias", {{"type", Json::array({"string", "null"})}}}
    });
    const Json create_properties{
        {"alias", {{"type", "string"}}},
        {"name", {{"type", "string"}}},
        {"position", nullable_schema(position_schema)},
        {"scale", nullable_schema(vector2_schema)},
        {"pivot", nullable_schema(vector2_schema)},
        {"rotation", nullable_schema({{"type", "number"}})},
        {"visible", nullable_schema({{"type", "boolean"}})},
        {"parent_entity_id", {{"type", Json::array({"integer", "null"})}, {"minimum", 1}}},
        {"parent_entity_alias", {{"type", Json::array({"string", "null"})}}},
        {"components", nullable_schema({
            {"type", "array"}, {"items", addable_schema}, {"maxItems", 12}
        })}
    };
    const Json instantiate_prefab_properties{
        {"asset_handle", {{"type", "string"}}},
        {"alias", {{"type", "string"}}},
        {"position", nullable_schema(position_schema)},
        {"scale", nullable_schema(vector2_schema)},
        {"rotation", nullable_schema({{"type", "number"}})},
        {"parent_entity_id", {{"type", Json::array({"integer", "null"})}, {"minimum", 1}}},
        {"parent_entity_alias", {{"type", Json::array({"string", "null"})}}}
    };
    Json generated_texture_assignment_properties = target_properties();
    generated_texture_assignment_properties["component_key"] = {
        {"type", "string"}, {"enum", {"sprite", "ui_image"}}
    };
    const Json generated_texture_assignment_schema{
        {"type", "object"},
        {"properties", generated_texture_assignment_properties},
        {"required", Json::array({"entity_id", "entity_alias", "component_key"})},
        {"additionalProperties", false}
    };
    const Json generated_texture_properties{
        {"generated_path", {{"type", "string"}}},
        {"destination_path", {{"type", "string"}}},
        {"assignments", {
            {"type", "array"},
            {"items", generated_texture_assignment_schema},
            {"maxItems", 20}
        }}
    };

    m_tool_schemas = Json::array({
        {
            {"type", "function"}, {"name", CREATE_ENTITY_TOOL},
            {"description", "Queues creation of a game entity. Position accepts [x,y] or [x,y,z]. Include the final name, transform, and components in this call. The alias can be referenced by later proposal tools in the same AI session. This does not mutate immediately."},
            {"parameters", {
                {"type", "object"},
                {"properties", create_properties},
                {"required", Json::array({
                    "alias", "name", "position", "scale", "pivot", "rotation", "visible",
                    "parent_entity_id", "parent_entity_alias", "components"
                })},
                {"additionalProperties", false}
            }}, {"strict", true}
        },
        {
            {"type", "function"}, {"name", SET_TRANSFORM_TOOL},
            {"description", "Queues a partial Transform edit. Use entity_id from find_scene_entities for an existing entity, or entity_alias only for an entity created earlier in this proposal. Position accepts [x,y] or [x,y,z]; omitting z preserves the current depth."},
            {"parameters", {{"type", "object"}, {"properties", transform_properties},
                {"required", Json::array({"entity_id", "entity_alias", "position", "scale", "pivot", "rotation", "visible"})},
                {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", RENAME_ENTITY_TOOL},
            {"description", "Queues a game entity rename. Use entity_id from find_scene_entities for an existing entity; entity_alias is only for an earlier create action."},
            {"parameters", {{"type", "object"}, {"properties", name_properties},
                {"required", Json::array({"entity_id", "entity_alias", "name"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", ADD_COMPONENT_TOOL},
            {"description", "Queues adding a registered component with validated engine defaults. Existing scene entities require entity_id from find_scene_entities; entity_alias is only for an earlier create action."},
            {"parameters", {{"type", "object"}, {"properties", component_properties},
                {"required", Json::array({"entity_id", "entity_alias", "component_key"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", REMOVE_COMPONENT_TOOL},
            {"description", "Queues removal of a removable registered component. Existing scene entities require entity_id from find_scene_entities; entity_alias is only for an earlier create action."},
            {"parameters", {{"type", "object"}, {"properties", removable_properties},
                {"required", Json::array({"entity_id", "entity_alias", "component_key"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", CONFIGURE_COMPONENT_TOOL},
            {"description", "Queues a validated merge patch for a registered component already present on the target. Call get_entity_details first, then pass patch_json as a JSON object string containing only fields to change. Existing scene entities require entity_id; entity_alias is only for an earlier create action."},
            {"parameters", { {"type", "object"}, {"properties", configurable_properties},
                {"required", Json::array({"entity_id", "entity_alias", "component_key", "patch_json"})},
                {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", REPARENT_ENTITY_TOOL},
            {"description", "Queues reparenting while preserving world transform. Resolve existing child and parent IDs with find_scene_entities. Aliases only reference earlier create actions. Use null for both parent fields or parent_entity_id 0 to move to the scene root."},
            {"parameters", {{"type", "object"}, {"properties", reparent_properties},
                {"required", Json::array({"entity_id", "entity_alias", "parent_entity_id", "parent_entity_alias"})},
                {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", DELETE_ENTITY_TOOL},
            {"description", "Queues deletion of an entity subtree. Use entity_id from find_scene_entities for an existing entity; never invent an alias for it. The complete subtree is restored by Undo."},
            {"parameters", {{"type", "object"}, {"properties", target_properties()},
                {"required", Json::array({"entity_id", "entity_alias"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", DUPLICATE_ENTITIES_TOOL},
            {"description", "Queues duplication of up to 20 existing entity subtrees in one action. Resolve all IDs with find_scene_entities. Nested selections are normalized to roots and Undo removes every duplicate."},
            {"parameters", {{"type", "object"}, {"properties", {{"entity_ids", entity_ids_schema}}},
                {"required", Json::array({"entity_ids"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", DELETE_ENTITIES_TOOL},
            {"description", "Queues deletion of up to 20 existing entity subtrees in one action. Resolve all IDs with find_scene_entities. Undo restores the complete set."},
            {"parameters", {{"type", "object"}, {"properties", {{"entity_ids", entity_ids_schema}}},
                {"required", Json::array({"entity_ids"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", INSTANTIATE_PREFAB_TOOL},
            {"description", "Queues a prefab instance by stable asset_handle. Call find_project_assets with asset_type prefab first. The alias identifies the new root for later actions in this proposal. Undo removes the full instance."},
            {"parameters", {{"type", "object"}, {"properties", instantiate_prefab_properties},
                {"required", Json::array({"asset_handle", "alias", "position", "scale", "rotation", "parent_entity_id", "parent_entity_alias"})},
                {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", UNPACK_PREFAB_TOOL},
            {"description", "Queues reversible unpacking of an existing prefab instance root. Use its entity_id from find_scene_entities."},
            {"parameters", {{"type", "object"}, {"properties", target_properties()},
                {"required", Json::array({"entity_id", "entity_alias"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", IMPORT_GENERATED_TEXTURE_TOOL},
            {"description", "Queues one import of a PNG produced by Codex ImageGen in this turn. Pass its exact savedPath and one new .png destination under Assets/. assignments may update up to 20 existing or request-local sprite/ui_image targets using this single imported asset. Call this tool exactly once per generated image and destination."},
            {"parameters", {{"type", "object"}, {"properties", generated_texture_properties},
                {"required", Json::array({"generated_path", "destination_path", "assignments"})},
                {"additionalProperties", false}}},
            {"strict", true}
        }
    });
}

bool EditorAiAuthoringRegistry::is_authoring_tool(const std::string& tool_name) const {
    return tool_name == CREATE_ENTITY_TOOL || tool_name == SET_TRANSFORM_TOOL ||
        tool_name == RENAME_ENTITY_TOOL || tool_name == ADD_COMPONENT_TOOL ||
        tool_name == REMOVE_COMPONENT_TOOL || tool_name == CONFIGURE_COMPONENT_TOOL ||
        tool_name == REPARENT_ENTITY_TOOL ||
        tool_name == DELETE_ENTITY_TOOL || tool_name == DUPLICATE_ENTITIES_TOOL ||
        tool_name == DELETE_ENTITIES_TOOL || tool_name == INSTANTIATE_PREFAB_TOOL ||
        tool_name == UNPACK_PREFAB_TOOL || tool_name == IMPORT_GENERATED_TEXTURE_TOOL;
}

EditorAiToolResult EditorAiAuthoringRegistry::queue_action(
    const std::string& tool_name,
    const Json& arguments,
    std::vector<EditorAiAuthoringAction>& actions,
    const Json* frozen_context
) const {
    if (!is_authoring_tool(tool_name)) return {false, {}, "Unknown authoring tool: " + tool_name};
    if (actions.size() >= MAX_ACTIONS_PER_PROPOSAL) {
        return {false, {}, "The proposal exceeded the safe action limit"};
    }
    if (!arguments.is_object()) return {false, {}, "Authoring arguments must be an object"};
    Json compact_arguments = arguments;
    bool removed_null = false;
    for (auto item = compact_arguments.begin(); item != compact_arguments.end();) {
        if (item.value().is_null()) {
            item = compact_arguments.erase(item);
            removed_null = true;
        } else {
            ++item;
        }
    }
    if (removed_null) return queue_action(tool_name, compact_arguments, actions, frozen_context);

    std::string error;
    std::string summary;
    Json stored_arguments = arguments;
    const auto alias_was_created = [&actions](const std::string& alias) {
        return std::any_of(actions.begin(), actions.end(), [&alias](const auto& action) {
            return (action.tool_name == CREATE_ENTITY_TOOL ||
                    action.tool_name == INSTANTIATE_PREFAB_TOOL) &&
                action.arguments.value("alias", "") == alias;
        });
    };
    const auto require_declared_alias = [&alias_was_created](
        const Json& value,
        const char* field
    ) -> std::optional<std::string> {
        if (!value.contains(field)) return std::nullopt;
        const std::string alias = value.at(field).get<std::string>();
        if (alias_was_created(alias)) return std::nullopt;
        return "Alias @" + alias +
            " is unavailable. For an existing scene entity, call find_scene_entities and use entity_id; "
            "entity_alias only references an entity created earlier in this proposal.";
    };
    const auto find_frozen_entity = [frozen_context](EntityID entity_id) -> const Json* {
        if (!frozen_context || !frozen_context->contains("entity_index") ||
            !frozen_context->at("entity_index").is_array()) return nullptr;
        for (const Json& entity : frozen_context->at("entity_index")) {
            if (entity.value("entity_id", EntityID{0}) == entity_id) return &entity;
        }
        return nullptr;
    };
    const auto frozen_index_available = [&]() {
        return frozen_context && frozen_context->contains("entity_index") &&
            frozen_context->at("entity_index").is_array();
    };
    if (tool_name == IMPORT_GENERATED_TEXTURE_TOOL) {
        if (!only_keys(arguments, {"generated_path", "destination_path", "assignments"}) ||
            !arguments.contains("generated_path") || !arguments.at("generated_path").is_string() ||
            arguments.at("generated_path").get<std::string>().empty() ||
            !arguments.contains("destination_path") || !arguments.at("destination_path").is_string() ||
            !valid_generated_texture_destination(arguments.at("destination_path").get<std::string>()) ||
            !arguments.contains("assignments") || !arguments.at("assignments").is_array() ||
            arguments.at("assignments").size() > 20) {
            return {false, {}, "Generated texture import requires ImageGen's savedPath, one new Assets/*.png destination, and at most 20 assignments"};
        }
        bool path_was_generated = false;
        if (frozen_context && frozen_context->contains("generated_texture_previews") &&
            frozen_context->at("generated_texture_previews").is_object()) {
            for (const auto& [id, preview] : frozen_context->at("generated_texture_previews").items()) {
                static_cast<void>(id);
                if (preview.is_object() && !preview.value("preview_path", "").empty() &&
                    preview.value("source_path", "") ==
                        arguments.at("generated_path").get<std::string>()) {
                    path_was_generated = true;
                    stored_arguments["generated_path"] = preview.value("preview_path", "");
                    break;
                }
            }
        }
        if (!path_was_generated) {
            return {false, {}, "generated_path was not produced by Codex ImageGen during this request"};
        }
        const std::string destination_path = arguments.at("destination_path").get<std::string>();
        const std::string preview_path = stored_arguments.at("generated_path").get<std::string>();
        for (const EditorAiAuthoringAction& action : actions) {
            if (action.tool_name != IMPORT_GENERATED_TEXTURE_TOOL) continue;
            if (action.arguments.value("destination_path", "") == destination_path) {
                return {false, {}, "Generated texture destination is already queued; use one import action with all assignments"};
            }
            if (action.arguments.value("generated_path", "") == preview_path) {
                return {false, {}, "Generated image is already queued; use one import action with all assignments"};
            }
        }
        std::unordered_set<std::string> assignment_targets;
        for (const Json& assignment : arguments.at("assignments")) {
            if (!only_keys(assignment, {"entity_id", "entity_alias", "component_key"}) ||
                !assignment.contains("component_key") || !assignment.at("component_key").is_string() ||
                (assignment.at("component_key") != "sprite" && assignment.at("component_key") != "ui_image")) {
                return {false, {}, "Each generated texture assignment requires one entity target and a sprite or ui_image component_key"};
            }
            const auto target = read_target(assignment, error);
            if (!target) return {false, {}, "Texture assignment requires exactly one entity target"};
            if (!target->alias.empty()) {
                if (const auto alias_error = require_declared_alias(assignment, "entity_alias")) {
                    return {false, {}, *alias_error};
                }
            } else if (frozen_index_available() && !find_frozen_entity(target->entity_id)) {
                return {false, {}, "Texture assignment target was not found in the frozen scene"};
            }
            const std::string assignment_key = target_label(*target) + ":" +
                assignment.at("component_key").get<std::string>();
            if (!assignment_targets.insert(assignment_key).second) {
                return {false, {}, "Generated texture assignments contain a duplicate entity component target"};
            }
        }
        summary = "Import generated texture to '" +
            destination_path + "'";
        const std::size_t assignment_count = arguments.at("assignments").size();
        if (assignment_count > 0) {
            summary += " and assign it to " + std::to_string(assignment_count) +
                (assignment_count == 1 ? " component" : " components");
        }
    } else if (tool_name == INSTANTIATE_PREFAB_TOOL) {
        if (!only_keys(arguments, {"asset_handle", "alias", "position", "scale", "rotation", "parent_entity_id", "parent_entity_alias"}) ||
            !arguments.contains("asset_handle") || !arguments.at("asset_handle").is_string() ||
            !arguments.contains("alias") || !arguments.at("alias").is_string() ||
            !valid_alias(arguments.at("alias").get<std::string>())) {
            return {false, {}, "Prefab instantiation requires a valid asset_handle and alias"};
        }
        if (arguments.contains("parent_entity_id") && arguments.contains("parent_entity_alias")) {
            return {false, {}, "Prefab instantiation accepts only one parent reference"};
        }
        if (arguments.contains("parent_entity_id") &&
            (!arguments.at("parent_entity_id").is_number_integer() ||
             arguments.at("parent_entity_id").get<std::int64_t>() <= 0 ||
             arguments.at("parent_entity_id").get<std::int64_t>() > std::numeric_limits<EntityID>::max())) {
            return {false, {}, "parent_entity_id must be within the supported positive range"};
        }
        if (arguments.contains("parent_entity_id") && frozen_index_available() &&
            !find_frozen_entity(static_cast<EntityID>(arguments.at("parent_entity_id").get<std::int64_t>()))) {
            return {false, {}, "Existing parent entity_id was not found in the frozen scene"};
        }
        if (arguments.contains("parent_entity_alias") &&
            (!arguments.at("parent_entity_alias").is_string() ||
             !valid_alias(arguments.at("parent_entity_alias").get<std::string>()))) {
            return {false, {}, "parent_entity_alias is invalid"};
        }
        if (const auto alias_error = require_declared_alias(arguments, "parent_entity_alias")) {
            return {false, {}, *alias_error};
        }
        if ((arguments.contains("position") && !finite_vector_range(arguments.at("position"), 2, 3)) ||
            (arguments.contains("scale") && !finite_vector(arguments.at("scale"), 2, 10'000.0)) ||
            (arguments.contains("rotation") && !finite_number(arguments.at("rotation")))) {
            return {false, {}, "Prefab transform values are invalid"};
        }
        if (arguments.contains("scale") &&
            (arguments.at("scale")[0].get<double>() == 0.0 || arguments.at("scale")[1].get<double>() == 0.0)) {
            return {false, {}, "Scale cannot contain zero"};
        }
        const std::string alias = arguments.at("alias").get<std::string>();
        if (alias_was_created(alias)) return {false, {}, "Entity alias is already used in this proposal: " + alias};
        if (!frozen_context || !frozen_context->contains("asset_index") ||
            !frozen_context->at("asset_index").is_array()) {
            return {false, {}, "Prefab instantiation requires a frozen project asset index"};
        }
        const std::string handle = arguments.at("asset_handle").get<std::string>();
        const Json* asset = nullptr;
        for (const Json& candidate : frozen_context->at("asset_index")) {
            if (candidate.value("asset_handle", "") == handle) { asset = &candidate; break; }
        }
        if (!asset) return {false, {}, "Unknown prefab asset handle; call find_project_assets"};
        if (asset->value("type", "") != "prefab" || asset->value("status", "") != "ready") {
            return {false, {}, "asset_handle must reference a ready prefab"};
        }
        summary = "Instantiate prefab '" + asset->value("path", handle) + "' as @" + alias;
    } else if (tool_name == DUPLICATE_ENTITIES_TOOL || tool_name == DELETE_ENTITIES_TOOL) {
        std::vector<EntityID> ids = read_entity_id_list(arguments, error);
        if (!error.empty()) return {false, {}, error};
        if (frozen_index_available()) {
            for (const EntityID id : ids) {
                if (!find_frozen_entity(id)) {
                    return {false, {}, "Existing entity_id #" + std::to_string(id) +
                        " was not found in the frozen scene; call find_scene_entities again"};
                }
            }
        }
        summary = std::string(tool_name == DUPLICATE_ENTITIES_TOOL ? "Duplicate " : "Delete ") +
            std::to_string(ids.size()) + (ids.size() == 1 ? " entity subtree" : " entity subtrees");
    } else if (tool_name == CREATE_ENTITY_TOOL) {
        if (!only_keys(arguments, {"alias", "name", "position", "scale", "pivot", "rotation", "visible", "parent_entity_id", "parent_entity_alias", "components"}) ||
            !arguments.contains("alias") || !arguments.at("alias").is_string() ||
            !valid_alias(arguments.at("alias").get<std::string>()) ||
            !arguments.contains("name") || !arguments.at("name").is_string() ||
            !valid_name(arguments.at("name").get<std::string>())) {
            return {false, {}, "Create entity requires a valid alias and name"};
        }
        if (arguments.contains("parent_entity_id") && arguments.contains("parent_entity_alias")) {
            return {false, {}, "Create entity accepts only one parent reference"};
        }
        if (arguments.contains("parent_entity_alias") &&
            (!arguments.at("parent_entity_alias").is_string() ||
             !valid_alias(arguments.at("parent_entity_alias").get<std::string>()))) {
            return {false, {}, "parent_entity_alias is invalid"};
        }
        if (const auto alias_error = require_declared_alias(arguments, "parent_entity_alias")) {
            return {false, {}, *alias_error};
        }
        if (arguments.contains("parent_entity_id") &&
            (!arguments.at("parent_entity_id").is_number_integer() ||
             arguments.at("parent_entity_id").get<std::int64_t>() <= 0 ||
             arguments.at("parent_entity_id").get<std::int64_t>() > std::numeric_limits<EntityID>::max())) {
            return {false, {}, "parent_entity_id must be within the supported positive range"};
        }
        if (arguments.contains("parent_entity_id") && frozen_index_available() &&
            !find_frozen_entity(static_cast<EntityID>(arguments.at("parent_entity_id").get<std::int64_t>()))) {
            return {false, {}, "Existing parent entity_id was not found in the frozen scene; call find_scene_entities again"};
        }
        if ((arguments.contains("position") && !finite_vector_range(arguments.at("position"), 2, 3)) ||
            (arguments.contains("scale") && !finite_vector(arguments.at("scale"), 2, 10'000.0)) ||
            (arguments.contains("pivot") && !finite_vector(arguments.at("pivot"), 2, 10'000.0)) ||
            (arguments.contains("rotation") && !finite_number(arguments.at("rotation"))) ||
            (arguments.contains("visible") && !arguments.at("visible").is_boolean())) {
            return {false, {}, "Create entity transform values are invalid"};
        }
        if (arguments.contains("scale")) {
            const auto& scale = arguments.at("scale");
            if (scale[0].get<double>() == 0.0 || scale[1].get<double>() == 0.0) {
                return {false, {}, "Scale cannot contain zero"};
            }
        }
        if (arguments.contains("components")) {
            if (!arguments.at("components").is_array() || arguments.at("components").size() > 12) {
                return {false, {}, "components must be an array with at most 12 entries"};
            }
            std::unordered_set<std::string> unique;
            for (const Json& component : arguments.at("components")) {
                if (!component.is_string() ||
                    std::find(m_addable_component_keys.begin(), m_addable_component_keys.end(), component.get<std::string>()) == m_addable_component_keys.end() ||
                    !unique.insert(component.get<std::string>()).second) {
                    return {false, {}, "components contains an unknown or duplicate component key"};
                }
            }
        }
        const std::string alias = arguments.at("alias").get<std::string>();
        if (std::any_of(actions.begin(), actions.end(), [&alias](const auto& action) {
                return action.tool_name == CREATE_ENTITY_TOOL && action.arguments.value("alias", "") == alias;
            })) {
            return {false, {}, "Entity alias is already used in this proposal: " + alias};
        }
        summary = "Create '" + arguments.at("name").get<std::string>() + "' as @" + alias;
    } else {
        const auto target = read_target(arguments, error);
        if (!target) return {false, {}, error};
        if (!target->alias.empty()) {
            if (const auto alias_error = require_declared_alias(arguments, "entity_alias")) {
                return {false, {}, *alias_error};
            }
        }
        const Json* frozen_entity = target->entity_id != 0 ? find_frozen_entity(target->entity_id) : nullptr;
        if (target->entity_id != 0 && frozen_index_available() && !frozen_entity) {
            return {false, {}, "Existing entity_id #" + std::to_string(target->entity_id) +
                " was not found in the frozen scene; call find_scene_entities again"};
        }
        if (tool_name == SET_TRANSFORM_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias", "position", "scale", "pivot", "rotation", "visible"})) {
                return {false, {}, "Set transform contains unsupported fields"};
            }
            const bool has_value = arguments.contains("position") || arguments.contains("scale") ||
                arguments.contains("pivot") || arguments.contains("rotation") || arguments.contains("visible");
            if (!has_value ||
                (arguments.contains("position") && !finite_vector_range(arguments.at("position"), 2, 3)) ||
                (arguments.contains("scale") && !finite_vector(arguments.at("scale"), 2, 10'000.0)) ||
                (arguments.contains("pivot") && !finite_vector(arguments.at("pivot"), 2, 10'000.0)) ||
                (arguments.contains("rotation") && !finite_number(arguments.at("rotation"))) ||
                (arguments.contains("visible") && !arguments.at("visible").is_boolean())) {
                return {false, {}, "Set transform requires at least one valid transform field"};
            }
            if (arguments.contains("scale") &&
                (arguments.at("scale")[0].get<double>() == 0.0 || arguments.at("scale")[1].get<double>() == 0.0)) {
                return {false, {}, "Scale cannot contain zero"};
            }
            summary = "Update Transform on " + target_label(*target);
        } else if (tool_name == RENAME_ENTITY_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias", "name"}) ||
                !arguments.contains("name") || !arguments.at("name").is_string() ||
                !valid_name(arguments.at("name").get<std::string>())) {
                return {false, {}, "Rename requires a valid name"};
            }
            summary = "Rename " + target_label(*target) + " to '" + arguments.at("name").get<std::string>() + "'";
        } else if (tool_name == ADD_COMPONENT_TOOL || tool_name == REMOVE_COMPONENT_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias", "component_key"}) ||
                !arguments.contains("component_key") || !arguments.at("component_key").is_string()) {
                return {false, {}, "Component action requires component_key"};
            }
            const std::string key = arguments.at("component_key").get<std::string>();
            const auto& allowed = tool_name == ADD_COMPONENT_TOOL
                ? m_addable_component_keys : m_removable_component_keys;
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
                return {false, {}, "Component is not allowed for this action: " + key};
            }
            summary = std::string(tool_name == ADD_COMPONENT_TOOL ? "Add " : "Remove ") + key + " on " + target_label(*target);
        } else if (tool_name == CONFIGURE_COMPONENT_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias", "component_key", "patch_json"}) ||
                !arguments.contains("component_key") || !arguments.at("component_key").is_string() ||
                !arguments.contains("patch_json") || !arguments.at("patch_json").is_string()) {
                return {false, {}, "Configure component requires component_key and patch_json"};
            }
            const std::string key = arguments.at("component_key").get<std::string>();
            if (std::find(
                    m_configurable_component_keys.begin(),
                    m_configurable_component_keys.end(),
                    key
                ) == m_configurable_component_keys.end()) {
                return {false, {}, "Component is not configurable through this action: " + key};
            }
            if (frozen_entity && frozen_entity->contains("components") &&
                std::find(
                    frozen_entity->at("components").begin(),
                    frozen_entity->at("components").end(),
                    key
                ) == frozen_entity->at("components").end()) {
                return {false, {}, "Entity #" + std::to_string(target->entity_id) +
                    " does not contain component '" + key + "'; add it before configuring it"};
            }
            const std::string patch_text = arguments.at("patch_json").get<std::string>();
            if (patch_text.empty() || patch_text.size() > 16 * 1024) {
                return {false, {}, "patch_json must contain at most 16384 bytes"};
            }
            const Json patch = Json::parse(patch_text, nullptr, false);
            if (patch.is_discarded() || !patch.is_object() || patch.empty()) {
                return {false, {}, "patch_json must encode a non-empty JSON object"};
            }
            std::vector<std::pair<std::string, std::string>> handles;
            collect_asset_handles(patch, handles);
            if (!handles.empty() && (!frozen_context || !frozen_context->contains("asset_index") ||
                !frozen_context->at("asset_index").is_array())) {
                return {false, {}, "Asset assignment requires a frozen project asset index"};
            }
            for (const auto& [field, handle] : handles) {
                const Json* asset = nullptr;
                for (const Json& candidate : frozen_context->at("asset_index")) {
                    if (candidate.value("asset_handle", "") == handle) {
                        asset = &candidate;
                        break;
                    }
                }
                if (!asset) {
                    return {false, {}, "Unknown asset handle '" + handle +
                        "'; call find_project_assets and use its asset_handle"};
                }
                if (asset->value("status", "") != "ready") {
                    return {false, {}, "Asset '" + handle + "' is not ready"};
                }
                if (const auto expected = expected_asset_type(key, field);
                    expected && asset->value("type", "") != *expected) {
                    return {false, {}, field + " requires asset type '" + *expected + "'"};
                }
            }
            summary = "Configure " + key + " on " + target_label(*target);
        } else if (tool_name == REPARENT_ENTITY_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias", "parent_entity_id", "parent_entity_alias"}) ||
                (arguments.contains("parent_entity_id") && arguments.contains("parent_entity_alias"))) {
                return {false, {}, "Reparent accepts at most one parent reference"};
            }
            if (arguments.contains("parent_entity_id") &&
                (!arguments.at("parent_entity_id").is_number_integer() ||
                 arguments.at("parent_entity_id").get<std::int64_t>() < 0 ||
                 arguments.at("parent_entity_id").get<std::int64_t>() > std::numeric_limits<EntityID>::max())) {
                return {false, {}, "parent_entity_id must be zero or within the supported positive range"};
            }
            if (arguments.contains("parent_entity_id") &&
                arguments.at("parent_entity_id").get<std::int64_t>() > 0 &&
                frozen_index_available() &&
                !find_frozen_entity(static_cast<EntityID>(arguments.at("parent_entity_id").get<std::int64_t>()))) {
                return {false, {}, "Existing parent entity_id was not found in the frozen scene; call find_scene_entities again"};
            }
            if (arguments.contains("parent_entity_alias") &&
                (!arguments.at("parent_entity_alias").is_string() ||
                 !valid_alias(arguments.at("parent_entity_alias").get<std::string>()))) {
                return {false, {}, "parent_entity_alias is invalid"};
            }
            if (const auto alias_error = require_declared_alias(arguments, "parent_entity_alias")) {
                return {false, {}, *alias_error};
            }
            summary = "Reparent " + target_label(*target);
        } else if (tool_name == DELETE_ENTITY_TOOL || tool_name == UNPACK_PREFAB_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias"})) {
                return {false, {}, "Entity action contains unsupported fields"};
            }
            summary = tool_name == DELETE_ENTITY_TOOL
                ? "Delete " + target_label(*target) + " and its subtree"
                : "Unpack prefab instance " + target_label(*target);
        }
    }

    actions.push_back({tool_name, std::move(stored_arguments), summary});
    return {
        true,
        {{"queued", true}, {"action_index", actions.size() - 1}, {"summary", summary}},
        {}
    };
}

std::unique_ptr<EditorCommandGroup> EditorAiAuthoringRegistry::build_command_group(
    const std::vector<EditorAiAuthoringAction>& actions,
    const EditorAiAuthoringContext& context,
    std::string& error
) const {
    if (actions.empty()) {
        error = "The AI proposal contains no authoring actions";
        return nullptr;
    }
    if (actions.size() > MAX_ACTIONS_PER_PROPOSAL) {
        error = "The AI proposal exceeds the safe action limit";
        return nullptr;
    }
    ComponentRegistry* registry = context.component_registry
        ? context.component_registry : m_component_registry;
    if (!context.entities || !registry || !context.selection || !context.entity_state_store) {
        error = "The active editor session is unavailable";
        return nullptr;
    }

    auto aliases = std::make_shared<AliasState>();
    std::unordered_set<std::string> declared_aliases;
    std::unordered_set<std::string> generated_texture_destinations;
    std::unordered_set<std::string> generated_texture_sources;
    auto group = std::make_unique<EditorCommandGroup>(
        "AI Session (" + std::to_string(actions.size()) +
        (actions.size() == 1 ? " action)" : " actions)")
    );

    const auto target_for = [&error](const Json& arguments) -> std::optional<EntityTarget> {
        return read_target(arguments, error);
    };
    const auto validate_target = [&](const EntityTarget& target) {
        if (!target.alias.empty()) {
            if (!declared_aliases.contains(target.alias)) {
                error = "Proposal references @" + target.alias + " before it is created";
                return false;
            }
            return true;
        }
        if (!context.entities->is_alive(target.entity_id)) {
            error = "Proposal references missing entity #" + std::to_string(target.entity_id);
            return false;
        }
        if (context.entity_state_store->is_locked(target.entity_id)) {
            error = "Proposal cannot modify locked entity #" + std::to_string(target.entity_id);
            return false;
        }
        return true;
    };
    const auto read_parent = [&](const Json& arguments) -> std::optional<EntityTarget> {
        EntityTarget parent;
        if (arguments.contains("parent_entity_id")) {
            const auto value = arguments.at("parent_entity_id").get<std::int64_t>();
            if (value > 0) parent.entity_id = static_cast<EntityID>(value);
        } else if (arguments.contains("parent_entity_alias")) {
            parent.alias = arguments.at("parent_entity_alias").get<std::string>();
        }
        if (parent.entity_id != 0 && !context.entities->is_alive(parent.entity_id)) {
            error = "Proposal references missing parent entity #" + std::to_string(parent.entity_id);
            return std::nullopt;
        }
        if (!parent.alias.empty() && !declared_aliases.contains(parent.alias)) {
            error = "Proposal references parent @" + parent.alias + " before it is created";
            return std::nullopt;
        }
        return parent;
    };

    for (const EditorAiAuthoringAction& action : actions) {
        if (!is_authoring_tool(action.tool_name)) {
            error = "Proposal contains an unknown action: " + action.tool_name;
            return nullptr;
        }
        if (action.tool_name == IMPORT_GENERATED_TEXTURE_TOOL) {
            AssetDatabase* database = context.serialization.asset_database;
            if (!database) {
                error = "The project asset database is unavailable";
                return nullptr;
            }
            const std::filesystem::path preview_path =
                action.arguments.at("generated_path").get<std::string>();
            constexpr std::size_t MAX_GENERATED_PNG_BYTES = 32 * 1024 * 1024;
            constexpr std::uint8_t PNG_SIGNATURE[] = {137, 80, 78, 71, 13, 10, 26, 10};
            std::error_code preview_error;
            const std::uintmax_t preview_size = std::filesystem::file_size(preview_path, preview_error);
            if (preview_error || preview_path.extension() != ".png" ||
                preview_size < std::size(PNG_SIGNATURE) || preview_size > MAX_GENERATED_PNG_BYTES) {
                error = "Generated texture preview is missing or invalid";
                return nullptr;
            }
            std::vector<std::uint8_t> png_bytes = FileSystem::read_bytes(preview_path.string());
            if (png_bytes.size() < std::size(PNG_SIGNATURE) || png_bytes.size() > MAX_GENERATED_PNG_BYTES ||
                !std::equal(std::begin(PNG_SIGNATURE), std::end(PNG_SIGNATURE), png_bytes.begin())) {
                error = "Generated texture preview is missing or invalid";
                return nullptr;
            }
            const std::filesystem::path relative = action.arguments.at("destination_path").get<std::string>();
            const std::filesystem::path destination = (database->project_root() / relative).lexically_normal();
            if (!valid_generated_texture_destination(relative.generic_string()) ||
                std::filesystem::exists(destination)) {
                error = "Generated texture destination is invalid or already exists";
                return nullptr;
            }
            if (!generated_texture_destinations.insert(destination.string()).second ||
                !generated_texture_sources.insert(preview_path.string()).second) {
                error = "Generated texture imports must use one action with all assignments";
                return nullptr;
            }
            group->add_command(std::make_unique<CreateBinaryAssetCommand>(
                database, destination, std::move(png_bytes)
            ));
            for (const Json& assignment : action.arguments.at("assignments")) {
                const auto target = target_for(assignment);
                if (!target || !validate_target(*target)) return nullptr;
                const EntityTarget captured_target = *target;
                const std::string component_key = assignment.at("component_key").get<std::string>();
                group->add_command(std::make_unique<DeferredEditorCommand>(
                    [entities = context.entities, registry, state_store = context.entity_state_store,
                     serialization = context.serialization, aliases, captured_target,
                     component_key, destination]() -> std::unique_ptr<IEditorCommand> {
                        AssetDatabase* asset_database = serialization.asset_database;
                        const AssetRecord* asset = asset_database
                            ? asset_database->find_by_path(destination) : nullptr;
                        const EntityID entity = resolve_target(captured_target, aliases);
                        if (!asset || asset->type != AssetType::Texture ||
                            asset->status != AssetStatus::Ready || !entities->is_alive(entity)) return nullptr;
                        return std::make_unique<PatchAiComponentCommand>(
                            entities, registry, state_store, serialization, aliases,
                            captured_target, component_key,
                            Json{{"texture_handle", asset->handle.to_string()}}
                        );
                    },
                    action.summary
                ));
            }
            continue;
        }
        if (action.tool_name == INSTANTIATE_PREFAB_TOOL) {
            if (!context.serialization.asset_database) {
                error = "The project asset database is unavailable";
                return nullptr;
            }
            const auto handle = AssetHandle::from_string(action.arguments.at("asset_handle").get<std::string>());
            const AssetRecord* asset = handle ? context.serialization.asset_database->find(*handle) : nullptr;
            if (!asset || asset->type != AssetType::Prefab || asset->status != AssetStatus::Ready) {
                error = "The prefab asset is no longer ready";
                return nullptr;
            }
            const std::string path = context.serialization.asset_database->resolve(*handle);
            if (path.empty()) {
                error = "The prefab asset path could not be resolved";
                return nullptr;
            }
            const std::string alias = action.arguments.at("alias").get<std::string>();
            if (!declared_aliases.insert(alias).second) {
                error = "Proposal declares duplicate alias @" + alias;
                return nullptr;
            }
            const auto parent = read_parent(action.arguments);
            if (!parent) return nullptr;
            std::optional<TransformComponent> transform;
            if (action.arguments.contains("position") || action.arguments.contains("scale") ||
                action.arguments.contains("rotation")) {
                transform = transform_from_create_arguments(action.arguments);
            }
            group->add_command(std::make_unique<InstantiateAiPrefabCommand>(
                context.entities, context.selection, context.entity_state_store.get(),
                context.serialization.renderer, aliases, alias, path, *parent, transform
            ));
            continue;
        }
        if (action.tool_name == DUPLICATE_ENTITIES_TOOL || action.tool_name == DELETE_ENTITIES_TOOL) {
            std::string ids_error;
            std::vector<EntityID> ids = read_entity_id_list(action.arguments, ids_error);
            if (!ids_error.empty()) {
                error = ids_error;
                return nullptr;
            }
            for (const EntityID id : ids) {
                if (!context.entities->is_alive(id)) {
                    error = "Proposal references missing entity #" + std::to_string(id);
                    return nullptr;
                }
                if (context.entity_state_store->is_locked(id)) {
                    error = "Proposal cannot modify locked entity #" + std::to_string(id);
                    return nullptr;
                }
            }
            if (action.tool_name == DUPLICATE_ENTITIES_TOOL) {
                auto command = std::make_unique<DuplicateEntitiesCommand>(
                    context.entities, context.selection, ids, context.entity_state_store.get()
                );
                if (!command->is_valid()) {
                    error = "The requested entity set cannot be duplicated";
                    return nullptr;
                }
                group->add_command(std::move(command));
            } else {
                auto command = std::make_unique<DeleteEntitiesCommand>(
                    context.entities, context.selection, ids, context.entity_state_store.get()
                );
                if (!command->is_valid()) {
                    error = "The requested entity set cannot be deleted";
                    return nullptr;
                }
                group->add_command(std::move(command));
            }
            continue;
        }
        if (action.tool_name == CREATE_ENTITY_TOOL) {
            const std::string alias = action.arguments.at("alias").get<std::string>();
            if (!declared_aliases.insert(alias).second) {
                error = "Proposal declares duplicate alias @" + alias;
                return nullptr;
            }
            const auto parent = read_parent(action.arguments);
            if (!parent) return nullptr;
            group->add_command(std::make_unique<CreateAiEntityCommand>(
                context.entities,
                registry,
                context.selection,
                context.entity_state_store,
                context.serialization,
                aliases,
                alias,
                action.arguments.at("name").get<std::string>(),
                transform_from_create_arguments(action.arguments),
                *parent,
                component_list(action.arguments)
            ));
            continue;
        }

        const auto target = target_for(action.arguments);
        if (!target || !validate_target(*target)) return nullptr;
        if (action.tool_name == SET_TRANSFORM_TOOL) {
            const Json arguments = action.arguments;
            const EntityTarget captured_target = *target;
            group->add_command(std::make_unique<DeferredEditorCommand>(
                [entities = context.entities, state_store = context.entity_state_store,
                 aliases, captured_target, arguments]() -> std::unique_ptr<IEditorCommand> {
                    const EntityID entity = resolve_target(captured_target, aliases);
                    if (!entities->is_alive(entity) || !entities->has_component<TransformComponent>(entity)) return nullptr;
                    TransformComponent before = entities->get_component<TransformComponent>(entity);
                    TransformComponent after = before;
                    apply_transform_arguments(after, arguments);
                    auto command = std::make_unique<TransformEntitiesCommand>(
                        entities,
                        std::unordered_map<EntityID, TransformComponent>{{entity, before}},
                        std::unordered_map<EntityID, TransformComponent>{{entity, after}},
                        "AI Set Transform",
                        state_store.get()
                    );
                    return command->is_valid() ? std::move(command) : nullptr;
                },
                action.summary
            ));
        } else if (action.tool_name == RENAME_ENTITY_TOOL) {
            const std::string new_name = action.arguments.at("name").get<std::string>();
            const EntityTarget captured_target = *target;
            group->add_command(std::make_unique<DeferredEditorCommand>(
                [entities = context.entities, aliases, captured_target, new_name]() -> std::unique_ptr<IEditorCommand> {
                    const EntityID entity = resolve_target(captured_target, aliases);
                    if (!entities->is_alive(entity) || !entities->has_component<TagComponent>(entity)) return nullptr;
                    const std::string old_name = entities->get_component<TagComponent>(entity).tag;
                    return std::make_unique<ModifyEntityCommand>(
                        entities,
                        entity,
                        [old_name](EntityManager& em, EntityID id) { em.get_component<TagComponent>(id).tag = old_name; },
                        [new_name](EntityManager& em, EntityID id) { em.get_component<TagComponent>(id).tag = new_name; },
                        "AI Rename Entity"
                    );
                },
                action.summary
            ));
        } else if (action.tool_name == ADD_COMPONENT_TOOL || action.tool_name == REMOVE_COMPONENT_TOOL) {
            group->add_command(std::make_unique<SetAiComponentPresenceCommand>(
                context.entities,
                registry,
                context.entity_state_store,
                context.serialization,
                aliases,
                *target,
                action.arguments.at("component_key").get<std::string>(),
                action.tool_name == ADD_COMPONENT_TOOL
            ));
        } else if (action.tool_name == CONFIGURE_COMPONENT_TOOL) {
            group->add_command(std::make_unique<PatchAiComponentCommand>(
                context.entities,
                registry,
                context.entity_state_store,
                context.serialization,
                aliases,
                *target,
                action.arguments.at("component_key").get<std::string>(),
                Json::parse(action.arguments.at("patch_json").get<std::string>())
            ));
        } else if (action.tool_name == REPARENT_ENTITY_TOOL) {
            const auto parent = read_parent(action.arguments);
            if (!parent) return nullptr;
            const EntityTarget captured_target = *target;
            const EntityTarget captured_parent = *parent;
            group->add_command(std::make_unique<DeferredEditorCommand>(
                [entities = context.entities, selection = context.selection,
                 state_store = context.entity_state_store, aliases, captured_target,
                 captured_parent]() -> std::unique_ptr<IEditorCommand> {
                    const EntityID entity = resolve_target(captured_target, aliases);
                    const EntityID parent_id = resolve_target(captured_parent, aliases);
                    auto command = std::make_unique<ReparentEntityCommand>(
                        entities, entity, parent_id, selection, "AI Reparent Entity", state_store.get()
                    );
                    return command->is_valid() ? std::move(command) : nullptr;
                },
                action.summary
            ));
        } else if (action.tool_name == DELETE_ENTITY_TOOL) {
            const EntityTarget captured_target = *target;
            group->add_command(std::make_unique<DeferredEditorCommand>(
                [entities = context.entities, selection = context.selection,
                 state_store = context.entity_state_store, aliases,
                 captured_target]() -> std::unique_ptr<IEditorCommand> {
                    const EntityID entity = resolve_target(captured_target, aliases);
                    auto command = std::make_unique<DeleteEntitiesCommand>(
                        entities, selection, std::vector<EntityID>{entity}, state_store.get()
                    );
                    return command->is_valid() ? std::move(command) : nullptr;
                },
                action.summary
            ));
        } else if (action.tool_name == UNPACK_PREFAB_TOOL) {
            const EntityTarget captured_target = *target;
            group->add_command(std::make_unique<DeferredEditorCommand>(
                [entities = context.entities, selection = context.selection,
                 state_store = context.entity_state_store, aliases,
                 captured_target]() -> std::unique_ptr<IEditorCommand> {
                    const EntityID entity = resolve_target(captured_target, aliases);
                    auto command = std::make_unique<UnpackPrefabCommand>(
                        entities, selection, entity, state_store.get()
                    );
                    return command->is_valid() ? std::move(command) : nullptr;
                },
                action.summary
            ));
        }
    }

    return group->empty() ? nullptr : std::move(group);
}

bool EditorAiAuthoringRegistry::execute_proposal(
    const std::vector<EditorAiAuthoringAction>& actions,
    const EditorAiAuthoringContext& context,
    EditorCommandHistory& history,
    std::uint64_t expected_revision,
    std::string& description,
    std::string& error
) const {
    description.clear();
    error.clear();
    if (history.get_current_revision() != expected_revision) {
        error = "The scene was edited after the proposal was generated";
        return false;
    }

    auto group = build_command_group(actions, context, error);
    if (!group) {
        if (error.empty()) error = "The action list could not be validated";
        return false;
    }
    description = group->get_description();
    if (!history.execute_command(std::move(group))) {
        description.clear();
        error = "Execution failed and every completed action was rolled back";
        return false;
    }
    return true;
}

} // namespace Engine

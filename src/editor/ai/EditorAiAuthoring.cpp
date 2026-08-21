#include "EditorAiAuthoring.hpp"

#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../core/Log.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
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
constexpr const char* REPARENT_ENTITY_TOOL = "propose_reparent_entity";
constexpr const char* DELETE_ENTITY_TOOL = "propose_delete_entity";

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
    const bool has_id = arguments.contains("entity_id");
    const bool has_alias = arguments.contains("entity_alias");
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

} // namespace

EditorAiAuthoringRegistry::EditorAiAuthoringRegistry(ComponentRegistry* component_registry)
    : m_component_registry(component_registry ? component_registry : &default_component_registry()) {
    for (const ComponentDescriptor& descriptor : m_component_registry->descriptors()) {
        if (descriptor.serialized_key == "transform" || descriptor.serialized_key == "tag" ||
            descriptor.serialized_key == "prefab_link") continue;
        m_addable_component_keys.push_back(descriptor.serialized_key);
        if (descriptor.removable) m_removable_component_keys.push_back(descriptor.serialized_key);
    }
    std::sort(m_addable_component_keys.begin(), m_addable_component_keys.end());
    std::sort(m_removable_component_keys.begin(), m_removable_component_keys.end());

    const Json vector2_schema{{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}};
    const Json position_schema{{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 3}};
    const Json addable_schema{{"type", "string"}, {"enum", m_addable_component_keys}};
    const Json removable_schema{{"type", "string"}, {"enum", m_removable_component_keys}};

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
            {"description", "Queues a partial Transform edit for an existing entity or an alias created earlier in this proposal. Position accepts [x,y] or [x,y,z]; omitting z preserves the current depth."},
            {"parameters", {{"type", "object"}, {"properties", transform_properties},
                {"required", Json::array({"entity_id", "entity_alias", "position", "scale", "pivot", "rotation", "visible"})},
                {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", RENAME_ENTITY_TOOL},
            {"description", "Queues a game entity rename."},
            {"parameters", {{"type", "object"}, {"properties", name_properties},
                {"required", Json::array({"entity_id", "entity_alias", "name"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", ADD_COMPONENT_TOOL},
            {"description", "Queues adding a registered component with validated engine defaults."},
            {"parameters", {{"type", "object"}, {"properties", component_properties},
                {"required", Json::array({"entity_id", "entity_alias", "component_key"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", REMOVE_COMPONENT_TOOL},
            {"description", "Queues removal of a removable registered component."},
            {"parameters", {{"type", "object"}, {"properties", removable_properties},
                {"required", Json::array({"entity_id", "entity_alias", "component_key"})}, {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", REPARENT_ENTITY_TOOL},
            {"description", "Queues reparenting while preserving world transform. Use null for both parent fields or parent_entity_id 0 to move to the scene root."},
            {"parameters", {{"type", "object"}, {"properties", reparent_properties},
                {"required", Json::array({"entity_id", "entity_alias", "parent_entity_id", "parent_entity_alias"})},
                {"additionalProperties", false}}},
            {"strict", true}
        },
        {
            {"type", "function"}, {"name", DELETE_ENTITY_TOOL},
            {"description", "Queues deletion of an entity subtree. The complete subtree is restored by Undo."},
            {"parameters", {{"type", "object"}, {"properties", target_properties()},
                {"required", Json::array({"entity_id", "entity_alias"})}, {"additionalProperties", false}}},
            {"strict", true}
        }
    });
}

bool EditorAiAuthoringRegistry::is_authoring_tool(const std::string& tool_name) const {
    return tool_name == CREATE_ENTITY_TOOL || tool_name == SET_TRANSFORM_TOOL ||
        tool_name == RENAME_ENTITY_TOOL || tool_name == ADD_COMPONENT_TOOL ||
        tool_name == REMOVE_COMPONENT_TOOL || tool_name == REPARENT_ENTITY_TOOL ||
        tool_name == DELETE_ENTITY_TOOL;
}

EditorAiToolResult EditorAiAuthoringRegistry::queue_action(
    const std::string& tool_name,
    const Json& arguments,
    std::vector<EditorAiAuthoringAction>& actions
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
    if (removed_null) return queue_action(tool_name, compact_arguments, actions);

    std::string error;
    std::string summary;
    if (tool_name == CREATE_ENTITY_TOOL) {
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
        if (arguments.contains("parent_entity_id") &&
            (!arguments.at("parent_entity_id").is_number_integer() || arguments.at("parent_entity_id").get<std::int64_t>() <= 0)) {
            return {false, {}, "parent_entity_id must be positive"};
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
        } else if (tool_name == REPARENT_ENTITY_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias", "parent_entity_id", "parent_entity_alias"}) ||
                (arguments.contains("parent_entity_id") && arguments.contains("parent_entity_alias"))) {
                return {false, {}, "Reparent accepts at most one parent reference"};
            }
            if (arguments.contains("parent_entity_id") &&
                (!arguments.at("parent_entity_id").is_number_integer() || arguments.at("parent_entity_id").get<std::int64_t>() < 0)) {
                return {false, {}, "parent_entity_id must be zero or positive"};
            }
            if (arguments.contains("parent_entity_alias") &&
                (!arguments.at("parent_entity_alias").is_string() ||
                 !valid_alias(arguments.at("parent_entity_alias").get<std::string>()))) {
                return {false, {}, "parent_entity_alias is invalid"};
            }
            summary = "Reparent " + target_label(*target);
        } else if (tool_name == DELETE_ENTITY_TOOL) {
            if (!only_keys(arguments, {"entity_id", "entity_alias"})) {
                return {false, {}, "Delete entity contains unsupported fields"};
            }
            summary = "Delete " + target_label(*target) + " and its subtree";
        }
    }

    actions.push_back({tool_name, arguments, summary});
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

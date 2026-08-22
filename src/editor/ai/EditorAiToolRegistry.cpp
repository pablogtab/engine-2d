#include "EditorAiToolRegistry.hpp"

#include "../EditorSelection.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../scene/Scene.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace Engine {
namespace {

nlohmann::json empty_object_schema() {
    return {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()},
        {"additionalProperties", false}
    };
}

struct EngineDocumentDescriptor {
    const char* id;
    const char* title;
    const char* relative_path;
    const char* description;
};

constexpr EngineDocumentDescriptor ENGINE_DOCUMENTS[] = {
    {"engine_contracts", "Engine contracts and architecture", "CLAUDE.md",
     "Project-wide workflow rules, architectural boundaries, renderer overview, and coding conventions."},
    {"roadmap", "Active engine roadmap", "ROADMAP.md",
     "Implemented systems, pending modules, completion criteria, and current architectural gates."},
    {"render_materials", "Render material contract", "docs/RENDER_MATERIALS.md",
     "Renderer material ownership, GPU backend boundaries, shader assets, passes, and validation."},
    {"runtime_display", "Runtime display settings", "docs/runtime-display-settings.md",
     "Display modes, persistence, rollback, and mobile behavior."},
    {"increment_checklist", "Increment completion checklist", "docs/INCREMENT_COMPLETION_CHECKLIST.md",
     "Undo, serialization, hierarchy, testing, repository hygiene, and delivery requirements."},
    {"ai_authoring", "AI-assisted authoring contract", "docs/AI_ASSISTED_AUTHORING.md",
     "Proposal tools, validation gates, atomic execution, session isolation, and rollback behavior."}
};

std::vector<EntityID> sorted_selection(const EditorSelection* selection) {
    if (!selection) return {};
    std::vector<EntityID> entities(selection->get_selected().begin(), selection->get_selected().end());
    std::sort(entities.begin(), entities.end());
    return entities;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

nlohmann::json compact_null_fields(nlohmann::json arguments) {
    if (!arguments.is_object()) return arguments;
    for (auto item = arguments.begin(); item != arguments.end();) {
        if (item.value().is_null()) item = arguments.erase(item);
        else ++item;
    }
    return arguments;
}

} // namespace

EditorAiToolRegistry::EditorAiToolRegistry(
    std::vector<EditorAiComponentProbe> component_probes,
    std::string project_root
)
    : m_component_probes(std::move(component_probes)),
      m_project_root(std::move(project_root)),
      m_tool_schemas(nlohmann::json::array({
          {
              {"type", "function"},
              {"name", "get_scene_summary"},
              {"description", "Returns a read-only summary of the active editor scene and selection."},
              {"parameters", empty_object_schema()},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "get_selection_context"},
              {"description", "Returns read-only entity, hierarchy, and component context for the current editor selection."},
              {"parameters", empty_object_schema()},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "find_scene_entities"},
              {"description", "Finds existing scene entities and returns their stable numeric entity_id values. Use this before modifying or deleting an existing entity; request-local aliases are only for entities created by this proposal."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"query", {{"type", nlohmann::json::array({"string", "null"})}}},
                      {"component_key", {{"type", nlohmann::json::array({"string", "null"})}}},
                      {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}
                  }},
                  {"required", {"query", "component_key", "limit"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "get_entity_details"},
              {"description", "Returns read-only hierarchy, transform, and component information for one existing numeric entity_id."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {{"entity_id", {{"type", "integer"}, {"minimum", 1}}}}},
                  {"required", {"entity_id"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "list_engine_documents"},
              {"description", "Lists the allowlisted Engine2D architecture and contract documents available for read-only consultation."},
              {"parameters", empty_object_schema()},
              {"strict", true}
          },
          {
              {"type", "function"},
              {"name", "read_engine_document"},
              {"description", "Reads one allowlisted Engine2D architecture or contract document by its document_id."},
              {"parameters", {
                  {"type", "object"},
                  {"properties", {
                      {"document_id", {
                          {"type", "string"},
                          {"enum", {"engine_contracts", "roadmap", "render_materials", "runtime_display", "increment_checklist", "ai_authoring"}}
                      }}
                  }},
                  {"required", {"document_id"}},
                  {"additionalProperties", false}
              }},
              {"strict", true}
          }
      })) {}

EditorAiToolResult EditorAiToolRegistry::execute(
    const std::string& tool_name,
    const nlohmann::json& arguments,
    const Scene* active_scene,
    const EditorSelection* selection
) const {
    if (!arguments.is_object()) return {false, {}, "Tool arguments must be an object"};
    if (tool_name == "read_engine_document") return read_engine_document(arguments);
    if (tool_name == "find_scene_entities" || tool_name == "get_entity_details") {
        if (!active_scene) return {false, {}, "No active scene is loaded"};
        const nlohmann::json entity_index = build_entity_index(active_scene, selection);
        return tool_name == "find_scene_entities"
            ? find_scene_entities(arguments, entity_index)
            : get_entity_details(arguments, entity_index);
    }
    if (!arguments.empty()) return {false, {}, "Tool arguments must be an empty object"};
    if (tool_name == "get_scene_summary") {
        return get_scene_summary(active_scene, selection);
    }
    if (tool_name == "get_selection_context") {
        return get_selection_context(active_scene, selection);
    }
    if (tool_name == "list_engine_documents") return list_engine_documents();
    return {false, {}, "Unknown editor AI tool: " + tool_name};
}

nlohmann::json EditorAiToolRegistry::build_context_snapshot(
    const Scene* active_scene,
    const EditorSelection* selection,
    const ComponentSerializationContext& serialization
) const {
    nlohmann::json snapshot{
        {"schema_version", 1},
        {"access", "read_only"},
        {"tools", m_tool_schemas}
    };

    const EditorAiToolResult scene_result = get_scene_summary(active_scene, selection);
    const EditorAiToolResult selection_result = get_selection_context(active_scene, selection);
    snapshot["scene"] = scene_result.success ? scene_result.output : nlohmann::json{{"error", scene_result.error}};
    snapshot["selection"] = selection_result.success
        ? selection_result.output
        : nlohmann::json{{"error", selection_result.error}};
    snapshot["entity_index"] = build_entity_index(active_scene, selection, serialization);
    snapshot["engine_documents"] = list_engine_documents().output;
    return snapshot;
}

EditorAiToolResult EditorAiToolRegistry::execute_snapshot(
    const std::string& tool_name,
    const nlohmann::json& arguments,
    const nlohmann::json& snapshot
) const {
    if (!arguments.is_object()) return {false, {}, "Tool arguments must be an object"};
    if (tool_name == "get_scene_summary" || tool_name == "get_selection_context") {
        if (!arguments.empty()) return {false, {}, "Tool arguments must be an empty object"};
        const char* key = tool_name == "get_scene_summary" ? "scene" : "selection";
        if (!snapshot.contains(key) || !snapshot.at(key).is_object()) {
            return {false, {}, "Frozen editor context is unavailable"};
        }
        if (snapshot.at(key).contains("error")) {
            return {false, {}, snapshot.at(key).value("error", "Frozen editor context is unavailable")};
        }
        return {true, snapshot.at(key), {}};
    }
    if (tool_name == "find_scene_entities" || tool_name == "get_entity_details") {
        if (!snapshot.contains("entity_index") || !snapshot.at("entity_index").is_array()) {
            return {false, {}, "Frozen entity index is unavailable"};
        }
        return tool_name == "find_scene_entities"
            ? find_scene_entities(arguments, snapshot.at("entity_index"))
            : get_entity_details(arguments, snapshot.at("entity_index"));
    }
    return execute(tool_name, arguments, nullptr, nullptr);
}

EditorAiToolResult EditorAiToolRegistry::list_engine_documents() const {
    nlohmann::json documents = nlohmann::json::array();
    for (const EngineDocumentDescriptor& descriptor : ENGINE_DOCUMENTS) {
        documents.push_back({
            {"document_id", descriptor.id},
            {"title", descriptor.title},
            {"description", descriptor.description}
        });
    }
    return {true, {{"documents", std::move(documents)}}, {}};
}

EditorAiToolResult EditorAiToolRegistry::read_engine_document(const nlohmann::json& arguments) const {
    if (arguments.size() != 1 || !arguments.contains("document_id") ||
        !arguments.at("document_id").is_string()) {
        return {false, {}, "read_engine_document requires only a string document_id"};
    }
    const std::string document_id = arguments.at("document_id").get<std::string>();
    const auto found = std::find_if(
        std::begin(ENGINE_DOCUMENTS),
        std::end(ENGINE_DOCUMENTS),
        [&document_id](const EngineDocumentDescriptor& descriptor) {
            return document_id == descriptor.id;
        }
    );
    if (found == std::end(ENGINE_DOCUMENTS)) {
        return {false, {}, "Unknown or disallowed engine document: " + document_id};
    }

    const std::filesystem::path path = std::filesystem::path(m_project_root) / found->relative_path;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {false, {}, "Engine document is unavailable: " + document_id};
    std::ostringstream contents;
    contents << stream.rdbuf();
    constexpr std::size_t MAX_DOCUMENT_BYTES = 256 * 1024;
    std::string text = contents.str();
    if (text.size() > MAX_DOCUMENT_BYTES) {
        return {false, {}, "Engine document exceeds the read-only context limit"};
    }
    return {
        true,
        {
            {"document_id", found->id},
            {"title", found->title},
            {"content", std::move(text)}
        },
        {}
    };
}

EditorAiToolResult EditorAiToolRegistry::get_scene_summary(
    const Scene* active_scene,
    const EditorSelection* selection
) const {
    if (!active_scene) {
        return {false, {}, "No active scene is loaded"};
    }

    const EntityManager& entities = active_scene->get_entity_manager();
    std::vector<EntityID> selected = sorted_selection(selection);
    std::erase_if(selected, [&entities](EntityID entity) { return !entities.is_alive(entity); });
    return {
        true,
        {
            {"name", active_scene->get_name()},
            {"entity_count", entities.get_living_entity_count()},
            {"root_entity_count", entities.get_root_entities().size()},
            {"selected_entity_count", selected.size()},
            {"selected_entity_ids", selected}
        },
        {}
    };
}

EditorAiToolResult EditorAiToolRegistry::get_selection_context(
    const Scene* active_scene,
    const EditorSelection* selection
) const {
    if (!active_scene) {
        return {false, {}, "No active scene is loaded"};
    }

    const EntityManager& entities = active_scene->get_entity_manager();
    nlohmann::json selected_entities = nlohmann::json::array();
    for (const EntityID entity : sorted_selection(selection)) {
        if (!entities.is_alive(entity)) continue;

        std::vector<std::string> component_keys;
        for (const EditorAiComponentProbe& probe : m_component_probes) {
            if (probe.has_component && probe.has_component(entities, entity)) {
                component_keys.push_back(probe.component_key);
            }
        }
        std::sort(component_keys.begin(), component_keys.end());

        std::string name = "Entity " + std::to_string(entity);
        if (entities.has_component<TagComponent>(entity)) {
            name = entities.get_component<TagComponent>(entity).tag;
        }

        EntityID parent = 0;
        std::vector<EntityID> children;
        if (entities.has_component<HierarchyComponent>(entity)) {
            const HierarchyComponent& hierarchy = entities.get_component<HierarchyComponent>(entity);
            parent = hierarchy.parent;
            children = hierarchy.children;
        }

        selected_entities.push_back({
            {"id", entity},
            {"name", std::move(name)},
            {"parent_id", parent},
            {"child_ids", std::move(children)},
            {"components", std::move(component_keys)}
        });
    }

    return {
        true,
        {
            {"count", selected_entities.size()},
            {"entities", std::move(selected_entities)}
        },
        {}
    };
}

nlohmann::json EditorAiToolRegistry::build_entity_index(
    const Scene* active_scene,
    const EditorSelection* selection,
    const ComponentSerializationContext& serialization
) const {
    nlohmann::json result = nlohmann::json::array();
    if (!active_scene) return result;

    const EntityManager& entities = active_scene->get_entity_manager();
    const std::vector<EntityID> selected = sorted_selection(selection);
    std::vector<EntityID> living = entities.get_living_entities();
    std::sort(living.begin(), living.end());
    for (const EntityID entity : living) {
        if (!entities.is_alive(entity)) continue;
        std::vector<std::string> component_keys;
        nlohmann::json component_data = nlohmann::json::object();
        for (const EditorAiComponentProbe& probe : m_component_probes) {
            if (probe.has_component && probe.has_component(entities, entity)) {
                component_keys.push_back(probe.component_key);
                if (probe.inspect_component) {
                    try {
                        nlohmann::json inspected = probe.inspect_component(
                            entities, entity, serialization
                        );
                        constexpr std::size_t MAX_COMPONENT_CONTEXT_BYTES = 64 * 1024;
                        const std::size_t serialized_bytes = inspected.dump().size();
                        if (serialized_bytes <= MAX_COMPONENT_CONTEXT_BYTES) {
                            component_data[probe.component_key] = std::move(inspected);
                        } else {
                            component_data[probe.component_key] = {
                                {"inspection_error", "Serialized component data exceeds the AI context limit"},
                                {"serialized_bytes", serialized_bytes}
                            };
                        }
                    } catch (const std::exception& exception) {
                        component_data[probe.component_key] = {
                            {"inspection_error", exception.what()}
                        };
                    }
                }
            }
        }
        std::sort(component_keys.begin(), component_keys.end());

        std::string name = "Entity " + std::to_string(entity);
        if (entities.has_component<TagComponent>(entity)) {
            name = entities.get_component<TagComponent>(entity).tag;
        }
        EntityID parent = 0;
        std::vector<EntityID> children;
        if (entities.has_component<HierarchyComponent>(entity)) {
            const HierarchyComponent& hierarchy = entities.get_component<HierarchyComponent>(entity);
            parent = hierarchy.parent;
            children = hierarchy.children;
        }

        nlohmann::json item{
            {"entity_id", entity},
            {"name", std::move(name)},
            {"parent_entity_id", parent},
            {"child_entity_ids", std::move(children)},
            {"components", std::move(component_keys)},
            {"component_data", std::move(component_data)},
            {"selected", std::binary_search(selected.begin(), selected.end(), entity)}
        };
        if (entities.has_component<TransformComponent>(entity)) {
            const TransformComponent& transform = entities.get_component<TransformComponent>(entity);
            item["transform"] = {
                {"position", {transform.position.x, transform.position.y, transform.position.z}},
                {"scale", {transform.scale.x, transform.scale.y}},
                {"rotation", transform.rotation},
                {"pivot", {transform.pivot.x, transform.pivot.y}},
                {"visible", transform.visible}
            };
        }
        result.push_back(std::move(item));
    }
    return result;
}

EditorAiToolResult EditorAiToolRegistry::find_scene_entities(
    const nlohmann::json& raw_arguments,
    const nlohmann::json& entity_index
) const {
    const nlohmann::json arguments = compact_null_fields(raw_arguments);
    if (!arguments.is_object()) return {false, {}, "find_scene_entities arguments must be an object"};
    for (auto item = arguments.begin(); item != arguments.end(); ++item) {
        if (item.key() != "query" && item.key() != "component_key" && item.key() != "limit") {
            return {false, {}, "find_scene_entities contains unsupported fields"};
        }
    }
    if (arguments.contains("query") && !arguments.at("query").is_string()) {
        return {false, {}, "query must be a string or null"};
    }
    if (arguments.contains("component_key") && !arguments.at("component_key").is_string()) {
        return {false, {}, "component_key must be a string or null"};
    }
    if (!arguments.contains("limit") || !arguments.at("limit").is_number_integer()) {
        return {false, {}, "limit must be an integer between 1 and 100"};
    }
    const std::int64_t requested_limit = arguments.at("limit").get<std::int64_t>();
    if (requested_limit < 1 || requested_limit > 100) {
        return {false, {}, "limit must be an integer between 1 and 100"};
    }

    const std::string query = lowercase(arguments.value("query", ""));
    const std::string component_key = arguments.value("component_key", "");
    nlohmann::json matches = nlohmann::json::array();
    std::size_t total_matches = 0;
    std::size_t exact_name_matches = 0;
    for (const nlohmann::json& entity : entity_index) {
        const std::string name = entity.value("name", "");
        const std::string normalized_name = lowercase(name);
        if (!query.empty() && normalized_name.find(query) == std::string::npos) continue;
        if (!component_key.empty()) {
            const auto& components = entity.at("components");
            if (std::find(components.begin(), components.end(), component_key) == components.end()) continue;
        }
        ++total_matches;
        if (!query.empty() && normalized_name == query) ++exact_name_matches;
        if (matches.size() < static_cast<std::size_t>(requested_limit)) {
            nlohmann::json summary = entity;
            summary.erase("component_data");
            matches.push_back(std::move(summary));
        }
    }
    return {
        true,
        {
            {"matches", std::move(matches)},
            {"returned_count", std::min(total_matches, static_cast<std::size_t>(requested_limit))},
            {"total_matches", total_matches},
            {"exact_name_matches", exact_name_matches},
            {"unambiguous", total_matches == 1},
            {"truncated", total_matches > static_cast<std::size_t>(requested_limit)},
            {"identity_rule", "Use entity_id for existing entities; entity_alias is only for entities created earlier in the same proposal."}
        },
        {}
    };
}

EditorAiToolResult EditorAiToolRegistry::get_entity_details(
    const nlohmann::json& arguments,
    const nlohmann::json& entity_index
) const {
    if (!arguments.is_object() || arguments.size() != 1 ||
        !arguments.contains("entity_id") || !arguments.at("entity_id").is_number_integer()) {
        return {false, {}, "get_entity_details requires only a positive integer entity_id"};
    }
    const std::int64_t requested_id = arguments.at("entity_id").get<std::int64_t>();
    if (requested_id <= 0 || requested_id > std::numeric_limits<EntityID>::max()) {
        return {false, {}, "entity_id is outside the supported range"};
    }
    for (const nlohmann::json& entity : entity_index) {
        if (entity.value("entity_id", EntityID{0}) == static_cast<EntityID>(requested_id)) {
            return {true, entity, {}};
        }
    }
    return {false, {}, "Existing entity_id was not found in the frozen scene: " + std::to_string(requested_id)};
}

} // namespace Engine

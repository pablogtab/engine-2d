#include "EditorAiToolRegistry.hpp"

#include "../EditorSelection.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../scene/Scene.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
    const EditorSelection* selection
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
    snapshot["engine_documents"] = list_engine_documents().output;
    return snapshot;
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

} // namespace Engine

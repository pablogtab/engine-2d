#pragma once

#include "../../ecs/EcsTypes.hpp"

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

class EditorSelection;
class EntityManager;
class Scene;

struct EditorAiComponentProbe {
    std::string component_key;
    std::function<bool(const EntityManager&, EntityID)> has_component;
};

struct EditorAiToolResult {
    bool success{false};
    nlohmann::json output;
    std::string error;
};

class EditorAiToolRegistry {
public:
    explicit EditorAiToolRegistry(
        std::vector<EditorAiComponentProbe> component_probes = {},
        std::string project_root = "."
    );

    [[nodiscard]] const nlohmann::json& tool_schemas() const { return m_tool_schemas; }
    [[nodiscard]] EditorAiToolResult execute(
        const std::string& tool_name,
        const nlohmann::json& arguments,
        const Scene* active_scene,
        const EditorSelection* selection
    ) const;
    [[nodiscard]] nlohmann::json build_context_snapshot(
        const Scene* active_scene,
        const EditorSelection* selection
    ) const;

private:
    [[nodiscard]] EditorAiToolResult get_scene_summary(
        const Scene* active_scene,
        const EditorSelection* selection
    ) const;
    [[nodiscard]] EditorAiToolResult get_selection_context(
        const Scene* active_scene,
        const EditorSelection* selection
    ) const;
    [[nodiscard]] EditorAiToolResult list_engine_documents() const;
    [[nodiscard]] EditorAiToolResult read_engine_document(const nlohmann::json& arguments) const;

    std::vector<EditorAiComponentProbe> m_component_probes;
    std::string m_project_root;
    nlohmann::json m_tool_schemas;
};

} // namespace Engine

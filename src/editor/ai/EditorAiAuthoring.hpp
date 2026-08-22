#pragma once

#include "EditorAiToolRegistry.hpp"
#include "../commands/EditorCommandGroup.hpp"
#include "../../scene/ComponentRegistry.hpp"

#include <memory>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

class EditorEntityStateStore;
class EditorSelection;
class EditorCommandHistory;
class EntityManager;

struct EditorAiAuthoringAction {
    std::string tool_name;
    nlohmann::json arguments;
    std::string summary;
};

struct EditorAiAuthoringContext {
    EntityManager* entities{nullptr};
    std::shared_ptr<EditorSelection> selection;
    std::shared_ptr<EditorEntityStateStore> entity_state_store;
    ComponentRegistry* component_registry{nullptr};
    ComponentSerializationContext serialization;
};

class EditorAiAuthoringRegistry {
public:
    static constexpr std::size_t MAX_ACTIONS_PER_PROPOSAL = 25;

    explicit EditorAiAuthoringRegistry(ComponentRegistry* component_registry = nullptr);

    [[nodiscard]] const nlohmann::json& tool_schemas() const { return m_tool_schemas; }
    [[nodiscard]] bool is_authoring_tool(const std::string& tool_name) const;
    [[nodiscard]] EditorAiToolResult queue_action(
        const std::string& tool_name,
        const nlohmann::json& arguments,
        std::vector<EditorAiAuthoringAction>& actions,
        const nlohmann::json* frozen_context = nullptr
    ) const;
    [[nodiscard]] std::unique_ptr<EditorCommandGroup> build_command_group(
        const std::vector<EditorAiAuthoringAction>& actions,
        const EditorAiAuthoringContext& context,
        std::string& error
    ) const;
    [[nodiscard]] bool execute_proposal(
        const std::vector<EditorAiAuthoringAction>& actions,
        const EditorAiAuthoringContext& context,
        EditorCommandHistory& history,
        std::uint64_t expected_revision,
        std::string& description,
        std::string& error
    ) const;

private:
    ComponentRegistry* m_component_registry{nullptr};
    nlohmann::json m_tool_schemas{nlohmann::json::array()};
    std::vector<std::string> m_addable_component_keys;
    std::vector<std::string> m_removable_component_keys;
    std::vector<std::string> m_configurable_component_keys;
};

} // namespace Engine

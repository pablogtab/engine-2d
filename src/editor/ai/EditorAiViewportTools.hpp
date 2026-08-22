#pragma once

#include "../../ecs/EcsTypes.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

enum class EditorAiViewportToolType {
    GetState,
    Pan,
    Zoom,
    FocusEntities,
    FrameAll,
    Reset,
    Capture,
    GetDiagnostics
};

struct EditorAiViewportToolCommand {
    EditorAiViewportToolType type{EditorAiViewportToolType::GetState};
    float x{0.0f};
    float y{0.0f};
    float factor{1.0f};
    float padding{0.15f};
    std::size_t limit{50};
    std::vector<EntityID> entity_ids;
};

class EditorAiViewportTools {
public:
    EditorAiViewportTools();

    [[nodiscard]] const nlohmann::json& tool_schemas() const { return m_tool_schemas; }
    [[nodiscard]] bool is_tool(const std::string& tool_name) const;
    [[nodiscard]] bool parse(
        const std::string& tool_name,
        const nlohmann::json& arguments,
        EditorAiViewportToolCommand& command,
        std::string& error
    ) const;

private:
    nlohmann::json m_tool_schemas;
};

} // namespace Engine

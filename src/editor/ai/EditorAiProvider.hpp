#pragma once

#include "EditorAiToolRegistry.hpp"

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

struct EditorAiChatMessage {
    std::string role;
    std::string content;
};

enum class EditorAiProviderEventType {
    Started,
    TextDelta,
    ToolStarted,
    ToolFinished
};

struct EditorAiProviderEvent {
    EditorAiProviderEventType type{EditorAiProviderEventType::Started};
    std::string text;
    std::string tool_name;
};

enum class EditorAiProviderStatus {
    Completed,
    Cancelled,
    Failed
};

struct EditorAiProviderRequest {
    std::string api_key;
    std::string model{"gpt-5.6-sol"};
    std::string prompt;
    std::string instructions;
    std::string reasoning_effort{"low"};
    std::vector<EditorAiChatMessage> history;
    nlohmann::json tools{nlohmann::json::array()};
    std::function<EditorAiToolResult(const std::string&, const nlohmann::json&)> execute_tool;
    std::string project_root{"."};
    std::string codex_executable;
    std::size_t max_tool_rounds{8};
    std::size_t max_tool_failures{3};
};

struct EditorAiProviderResult {
    EditorAiProviderStatus status{EditorAiProviderStatus::Failed};
    std::string text;
    std::string error;
    std::string response_id;
    long http_status{0};
    std::size_t tool_call_count{0};
    std::size_t input_tokens{0};
    std::size_t output_tokens{0};
    std::size_t total_tokens{0};
};

class IEditorAiProvider {
public:
    virtual ~IEditorAiProvider() = default;
    [[nodiscard]] virtual bool is_available() const = 0;
    [[nodiscard]] virtual EditorAiProviderResult run(
        const EditorAiProviderRequest& request,
        const std::function<void(const EditorAiProviderEvent&)>& on_event,
        const std::function<bool()>& is_cancelled = {}
    ) = 0;
};

} // namespace Engine

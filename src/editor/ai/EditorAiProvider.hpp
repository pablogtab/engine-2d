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
    ReasoningSummaryDelta,
    ItemStarted,
    ItemCompleted,
    ToolStarted,
    ToolFinished,
    ImageGenerated
};

struct EditorAiProviderEvent {
    EditorAiProviderEventType type{EditorAiProviderEventType::Started};
    std::string text;
    std::string tool_name;
    std::string artifact_id;
    std::string artifact_path;
    std::string item_id;
    std::string item_type;
    std::string item_status;
    bool transparent_background{false};
};

struct EditorAiSkillReference {
    std::string name;
    std::string path;
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
    std::string reasoning_summary{"auto"};
    std::vector<EditorAiChatMessage> history;
    std::vector<EditorAiSkillReference> skills;
    nlohmann::json tools{nlohmann::json::array()};
    std::function<EditorAiToolResult(const std::string&, const nlohmann::json&)> execute_tool;
    std::string project_root{"."};
    std::string codex_executable;
    std::string provider_thread_id;
    bool persist_thread{false};
    bool compact_thread_before_turn{false};
    std::size_t max_tool_rounds{8};
    std::size_t max_tool_failures{3};
};

struct EditorAiProviderResult {
    EditorAiProviderStatus status{EditorAiProviderStatus::Failed};
    std::string text;
    std::string error;
    std::string response_id;
    std::string provider_thread_id;
    std::string provider_session_id;
    long http_status{0};
    std::size_t tool_call_count{0};
    std::size_t input_tokens{0};
    std::size_t output_tokens{0};
    std::size_t total_tokens{0};
    bool context_compacted{false};
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

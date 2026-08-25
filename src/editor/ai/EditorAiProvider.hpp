#pragma once

#include "EditorAiToolRegistry.hpp"

#include <functional>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

struct EditorAiChatMessage {
    std::string role;
    std::string content;
};

enum class EditorAiCollaborationMode {
    Default,
    Plan
};

struct EditorAiPlanStep {
    std::string step;
    std::string status{"pending"};
};

struct EditorAiUserInputOption {
    std::string label;
    std::string description;
};

struct EditorAiUserInputQuestion {
    std::string id;
    std::string header;
    std::string question;
    std::vector<EditorAiUserInputOption> options;
    bool allow_other{false};
    bool secret{false};
};

struct EditorAiUserInputRequest {
    std::string item_id;
    std::vector<EditorAiUserInputQuestion> questions;
    bool blocking{true};
    std::optional<std::uint64_t> auto_resolution_ms;
};

using EditorAiUserInputAnswers =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

enum class EditorAiProviderEventType {
    Started,
    TextDelta,
    ReasoningSummaryDelta,
    ItemStarted,
    ItemCompleted,
    ItemOutputDelta,
    ToolStarted,
    ToolFinished,
    ImageGenerated,
    PlanDelta,
    PlanUpdated,
    PlanCompleted
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
    std::string explanation;
    std::vector<EditorAiPlanStep> plan;
    nlohmann::json details{nlohmann::json::object()};
    bool transparent_background{false};
};

struct EditorAiSkillReference {
    std::string name;
    std::string path;
};

struct EditorAiImageAttachment {
    std::string local_path;
    std::string data_url;
    std::string detail{"auto"};
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
    std::vector<EditorAiImageAttachment> images;
    nlohmann::json tools{nlohmann::json::array()};
    std::function<EditorAiToolResult(const std::string&, const nlohmann::json&)> execute_tool;
    std::function<std::optional<EditorAiUserInputAnswers>(const EditorAiUserInputRequest&)>
        request_user_input;
    std::string project_root{"."};
    std::string codex_executable;
    std::string provider_thread_id;
    bool persist_thread{false};
    bool compact_thread_before_turn{false};
    std::size_t max_tool_rounds{8};
    std::size_t max_tool_failures{3};
    std::uint64_t dynamic_tool_start_timeout_ms{120000};
    std::uint64_t provider_inactivity_timeout_ms{180000};
    EditorAiCollaborationMode collaboration_mode{EditorAiCollaborationMode::Default};
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
    std::string plan_text;
    std::string plan_explanation;
    std::vector<EditorAiPlanStep> plan;
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

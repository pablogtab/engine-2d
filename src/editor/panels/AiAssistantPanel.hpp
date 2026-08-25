#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../ai/CodexAppServerProvider.hpp"
#include "../ai/EditorAiConversationStore.hpp"
#include "../ai/EditorAiAuthoring.hpp"
#include "../ai/EditorAiAuthoringSession.hpp"
#include "../ai/EditorAiAttachmentStore.hpp"
#include "../ai/EditorAiToolRegistry.hpp"
#include "../ai/EditorAiViewportTools.hpp"
#include "../ai/OpenAiResponsesProvider.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Engine {

class SceneManager;
class Scene;
class EditorCommandHistory;
class EditorEntityStateStore;
class Texture;
class ViewportPanel;

class AiAssistantPanel final : public EditorPanel {
public:
    AiAssistantPanel(
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection,
        ComponentSerializationContext serialization_context = {}
    );
    ~AiAssistantPanel() override;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }
    void request_focus() {
        is_open = true;
        m_focus_requested = true;
    }
    void request_attach_selection() {
        is_open = true;
        m_focus_requested = true;
        m_attach_selection_requested = true;
    }
    void request_attach_assets(std::vector<std::filesystem::path> paths) {
        is_open = true;
        m_focus_requested = true;
        m_pending_asset_context_paths = std::move(paths);
        m_attach_assets_requested = true;
    }
    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = std::move(selection); }
    void set_command_history(std::shared_ptr<EditorCommandHistory> history) {
        m_command_history = std::move(history);
    }
    void set_entity_state_store(std::shared_ptr<EditorEntityStateStore> state_store) {
        m_entity_state_store = std::move(state_store);
    }
    void set_viewport_panel(const std::shared_ptr<ViewportPanel>& viewport_panel) {
        m_viewport_panel = viewport_panel;
    }

private:
    enum class LiveEditorToolState : std::uint8_t {
        Queued,
        Executing,
        Abandoned,
        Completed
    };

    enum class ProviderBackend {
        CodexSubscription,
        OpenAiApi
    };

    enum class AuthoringMode {
        ConfirmBeforeApply,
        AutonomousSafe
    };

    enum class ReasoningEffort {
        Low,
        Medium,
        High
    };

    struct PendingProposal {
        std::uint64_t request_id{0};
        std::vector<EditorAiAuthoringAction> actions;
        std::weak_ptr<Scene> target_scene;
        std::shared_ptr<EditorCommandHistory> command_history;
        std::shared_ptr<EditorEntityStateStore> entity_state_store;
        std::shared_ptr<EditorSelection> selection;
        std::uint64_t baseline_revision{0};
        std::string scene_name;
        nlohmann::json before_entity_index{nlohmann::json::array()};
        nlohmann::json before_asset_index{nlohmann::json::array()};
        nlohmann::json generated_texture_previews{nlohmann::json::object()};
        bool auto_apply{false};
        bool iterative_continuation{false};
        std::string original_prompt;
        std::uint64_t session_revision_before{0};
    };

    struct IterativeAuthoringBinding {
        EditorAiAuthoringSession session;
        std::weak_ptr<Scene> target_scene;
        std::weak_ptr<EditorCommandHistory> command_history;
        std::weak_ptr<EditorEntityStateStore> entity_state_store;
        std::weak_ptr<EditorSelection> selection;
    };

    struct AppliedAiSession {
        std::uint64_t request_id{0};
        std::weak_ptr<Scene> target_scene;
        std::weak_ptr<EditorCommandHistory> command_history;
        std::uint64_t after_revision{0};
        std::string description;
    };

    struct PendingUserInput {
        explicit PendingUserInput(EditorAiUserInputRequest value)
            : request(std::move(value)), answers(request.questions.size()),
              custom_answers(request.questions.size()), custom_selected(request.questions.size()) {
            for (std::size_t index = 0; index < request.questions.size(); ++index) {
                custom_selected[index] = request.questions[index].options.empty();
            }
        }

        EditorAiUserInputRequest request;
        std::vector<std::string> answers;
        std::vector<std::array<char, 256>> custom_answers;
        std::vector<bool> custom_selected;
        std::promise<std::optional<EditorAiUserInputAnswers>> completion;
        std::atomic_bool resolved{false};
    };

    void attach_current_selection();
    void attach_asset_paths(const std::vector<std::filesystem::path>& paths);
    void attach_clipboard_screenshot();
    void attach_viewport_screenshot();
    void prune_attached_contexts();

    struct AiActivityEntry {
        std::string label;
        std::string detail;
        std::uint64_t elapsed_ms{0};
        std::string item_id;
        std::string item_type;
        std::string status{"inProgress"};
        std::uint64_t duration_ms{0};
        std::string output;
    };

    struct LiveEditorToolRequest {
        std::string name;
        nlohmann::json arguments;
        EditorAiViewportToolCommand command;
        std::weak_ptr<Scene> expected_scene;
        std::uint64_t expected_revision{0};
        std::chrono::steady_clock::time_point queued_at;
        std::atomic<LiveEditorToolState> state{LiveEditorToolState::Queued};
        std::promise<EditorAiToolResult> completion;
    };

    [[nodiscard]] std::string current_context_json() const;
    void start_request(
        std::string internal_prompt = {},
        bool iterative_continuation = false,
        bool approved_plan_build = false
    );
    void continue_iterative_authoring();
    void finalize_iterative_verification();
    void cancel_request();
    void start_new_conversation();
    void restore_conversation(const std::string& thread_id);
    void rename_conversation(const std::string& thread_id, const std::string& title);
    void delete_conversation(const std::string& thread_id);
    void persist_current_conversation_locked(const std::string& model);
    void copy_to_clipboard(const std::string& contents, const std::string& feedback);
    void toggle_authoring_mode();
    void set_collaboration_mode(EditorAiCollaborationMode mode);
    void start_approved_plan_build();
    [[nodiscard]] std::optional<EditorAiUserInputAnswers> request_user_input(
        const EditorAiUserInputRequest& request
    );
    void resolve_user_input(bool submit);
    void apply_pending_proposal();
    void dismiss_pending_proposal();
    void undo_last_ai_session();
    [[nodiscard]] nlohmann::json viewport_state_json() const;
    [[nodiscard]] EditorAiToolResult execute_live_editor_tool(
        const std::string& name,
        const nlohmann::json& arguments,
        const std::shared_ptr<Scene>& expected_scene,
        std::uint64_t expected_revision
    );
    [[nodiscard]] EditorAiToolResult execute_live_editor_tool_on_main_thread(
        const LiveEditorToolRequest& request
    );
    void process_live_editor_tools();
    void cancel_live_editor_tools(const std::string& reason);

    std::string m_name{"AI Assistant"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store;
    ComponentSerializationContext m_serialization_context;
    EditorAiToolRegistry m_tool_registry;
    EditorAiViewportTools m_viewport_tools;
    EditorAiAuthoringRegistry m_authoring_registry;
    EditorAiAttachmentStore m_attachments;
    std::weak_ptr<ViewportPanel> m_viewport_panel;
    std::unique_ptr<CodexAppServerProvider> m_codex_provider;
    std::unique_ptr<OpenAiResponsesProvider> m_openai_provider;
    std::unique_ptr<EditorAiConversationStore> m_conversation_store;
    std::string m_codex_executable;
    std::string m_imagegen_skill_path;
    std::array<char, 1024> m_prompt{};
    std::array<char, 64> m_model{};
    std::array<char, 128> m_conversation_title{};
    ProviderBackend m_backend{ProviderBackend::CodexSubscription};
    AuthoringMode m_authoring_mode{AuthoringMode::ConfirmBeforeApply};
    ReasoningEffort m_reasoning_effort{ReasoningEffort::Low};
    int m_authoring_work_budget_minutes{10};
    EditorAiCollaborationMode m_collaboration_mode{EditorAiCollaborationMode::Default};

    mutable std::mutex m_request_mutex;
    std::mutex m_live_tool_mutex;
    std::deque<std::shared_ptr<LiveEditorToolRequest>> m_live_tool_requests;
    std::vector<EditorAiChatMessage> m_messages;
    std::string m_streamed_response;
    std::string m_reasoning_summary;
    std::string m_request_error;
    std::string m_active_tool;
    std::string m_live_activity{"Idle"};
    std::vector<AiActivityEntry> m_activity_entries;
    std::chrono::steady_clock::time_point m_request_started_at{};
    std::string m_authoring_feedback;
    std::string m_last_receipt_summary;
    nlohmann::json m_last_application_receipt{nlohmann::json::object()};
    std::string m_ui_feedback;
    std::string m_codex_thread_id;
    std::string m_codex_session_id;
    std::string m_superseded_thread_id;
    std::string m_conversation_action_thread_id;
    std::optional<PendingProposal> m_pending_proposal;
    std::optional<AppliedAiSession> m_last_applied_session;
    std::optional<IterativeAuthoringBinding> m_iterative_authoring;
    std::shared_ptr<PendingUserInput> m_pending_user_input;
    std::vector<EditorAiPlanStep> m_plan;
    std::string m_plan_text;
    std::string m_plan_explanation;
    std::size_t m_plan_revision{0};
    std::string m_approved_plan_text;
    std::vector<std::filesystem::path> m_pending_asset_context_paths;
    std::unique_ptr<Texture> m_generated_texture_preview;
    std::string m_loaded_texture_preview_id;
    std::size_t m_last_input_tokens{0};
    std::size_t m_last_output_tokens{0};
    std::size_t m_last_total_tokens{0};
    std::size_t m_last_tool_calls{0};
    std::uint64_t m_last_request_duration_ms{0};
    bool m_request_running{false};
    bool m_scroll_chat_to_bottom{false};
    bool m_focus_requested{false};
    bool m_focus_prompt_requested{false};
    bool m_attach_selection_requested{false};
    bool m_attach_assets_requested{false};
    bool m_open_session_settings_requested{false};
    bool m_image_generation_requested{false};
    bool m_conversation_has_generated_image{false};
    bool m_compact_before_next_request{false};
    bool m_iteration_continuation_requested{false};
    bool m_iteration_verification_pending{false};
    bool m_iteration_verification_inspected{false};
    bool m_iteration_verification_time_budget_reached{false};
    bool m_plan_build_requested{false};
    std::uint64_t m_iteration_verification_revision{0};
    std::atomic_bool m_cancel_requested{false};
    std::thread m_request_thread;
    std::uint64_t m_viewport_capture_sequence{0};
};

} // namespace Engine

#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../ai/CodexAppServerProvider.hpp"
#include "../ai/EditorAiConversationStore.hpp"
#include "../ai/EditorAiAuthoring.hpp"
#include "../ai/EditorAiToolRegistry.hpp"
#include "../ai/OpenAiResponsesProvider.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Engine {

class SceneManager;
class Scene;
class EditorCommandHistory;
class EditorEntityStateStore;

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
    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = std::move(selection); }
    void set_command_history(std::shared_ptr<EditorCommandHistory> history) {
        m_command_history = std::move(history);
    }
    void set_entity_state_store(std::shared_ptr<EditorEntityStateStore> state_store) {
        m_entity_state_store = std::move(state_store);
    }

private:
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
        bool auto_apply{false};
    };

    struct AppliedAiSession {
        std::uint64_t request_id{0};
        std::weak_ptr<Scene> target_scene;
        std::weak_ptr<EditorCommandHistory> command_history;
        std::uint64_t after_revision{0};
        std::string description;
    };

    [[nodiscard]] std::string current_context_json() const;
    void start_request();
    void cancel_request();
    void start_new_conversation();
    void restore_conversation(const std::string& thread_id);
    void rename_conversation(const std::string& thread_id, const std::string& title);
    void delete_conversation(const std::string& thread_id);
    void persist_current_conversation_locked(const std::string& model);
    void copy_to_clipboard(const std::string& contents, const std::string& feedback);
    void toggle_authoring_mode();
    void apply_pending_proposal();
    void dismiss_pending_proposal();
    void undo_last_ai_session();

    std::string m_name{"AI Assistant"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store;
    ComponentSerializationContext m_serialization_context;
    EditorAiToolRegistry m_tool_registry;
    EditorAiAuthoringRegistry m_authoring_registry;
    std::unique_ptr<CodexAppServerProvider> m_codex_provider;
    std::unique_ptr<OpenAiResponsesProvider> m_openai_provider;
    std::unique_ptr<EditorAiConversationStore> m_conversation_store;
    std::string m_codex_executable;
    std::array<char, 1024> m_prompt{};
    std::array<char, 64> m_model{};
    std::array<char, 128> m_conversation_title{};
    ProviderBackend m_backend{ProviderBackend::CodexSubscription};
    AuthoringMode m_authoring_mode{AuthoringMode::ConfirmBeforeApply};
    ReasoningEffort m_reasoning_effort{ReasoningEffort::Low};

    mutable std::mutex m_request_mutex;
    std::vector<EditorAiChatMessage> m_messages;
    std::string m_streamed_response;
    std::string m_request_error;
    std::string m_active_tool;
    std::string m_authoring_feedback;
    std::string m_ui_feedback;
    std::string m_codex_thread_id;
    std::string m_codex_session_id;
    std::string m_conversation_action_thread_id;
    std::optional<PendingProposal> m_pending_proposal;
    std::optional<AppliedAiSession> m_last_applied_session;
    std::size_t m_last_input_tokens{0};
    std::size_t m_last_output_tokens{0};
    std::size_t m_last_total_tokens{0};
    std::size_t m_last_tool_calls{0};
    std::uint64_t m_last_request_duration_ms{0};
    bool m_request_running{false};
    bool m_scroll_chat_to_bottom{false};
    bool m_focus_requested{false};
    bool m_focus_prompt_requested{false};
    bool m_open_session_settings_requested{false};
    std::atomic_bool m_cancel_requested{false};
    std::thread m_request_thread;
};

} // namespace Engine

#include "AiAssistantPanel.hpp"

#include "../../scene/ComponentRegistry.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../core/Log.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../tools/EditorIcons.hpp"

#include <imgui.h>

#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <numeric>
#include <unordered_map>

namespace Engine {
namespace {

std::atomic_uint64_t NEXT_AI_REQUEST_ID{1};
// App Server binds dynamic tools on thread/start. Bump this whenever the editor
// tool schemas or their identity semantics change so saved threads are replaced.
constexpr std::uint32_t EDITOR_AI_TOOL_CONTRACT_VERSION = 3;

std::vector<EditorAiChatMessage> recent_history(
    const std::vector<EditorAiChatMessage>& messages
) {
    constexpr std::size_t MAX_MESSAGES = 6;
    constexpr std::size_t MAX_TOTAL_CHARACTERS = 12'000;
    constexpr std::size_t MAX_MESSAGE_CHARACTERS = 4'000;
    std::vector<EditorAiChatMessage> reversed;
    std::size_t total_characters = 0;
    for (auto message = messages.rbegin();
         message != messages.rend() && reversed.size() < MAX_MESSAGES; ++message) {
        EditorAiChatMessage copy = *message;
        if (copy.content.size() > MAX_MESSAGE_CHARACTERS) {
            copy.content.resize(MAX_MESSAGE_CHARACTERS);
            copy.content += "\n[truncated by editor context budget]";
        }
        if (!reversed.empty() && total_characters + copy.content.size() > MAX_TOTAL_CHARACTERS) break;
        total_characters += copy.content.size();
        reversed.push_back(std::move(copy));
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

std::vector<EditorAiComponentProbe> build_component_probes() {
    std::vector<EditorAiComponentProbe> probes;
    for (const ComponentDescriptor& descriptor : default_component_registry().descriptors()) {
        probes.push_back({descriptor.serialized_key, descriptor.has, descriptor.serialize});
    }
    return probes;
}

std::string project_root() {
#if defined(ENGINE_SOURCE_DIR)
    return ENGINE_SOURCE_DIR;
#else
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    return error ? "." : current.string();
#endif
}

std::string abbreviated_id(const std::string& id) {
    constexpr std::size_t VISIBLE_CHARACTERS = 8;
    if (id.size() <= VISIBLE_CHARACTERS * 2 + 3) return id;
    return id.substr(0, VISIBLE_CHARACTERS) + "..." +
        id.substr(id.size() - VISIBLE_CHARACTERS);
}

std::string codex_resume_command(const std::string& thread_id) {
    return "codex resume --include-non-interactive " + thread_id;
}

bool has_visible_text(const char* text) {
    if (!text) return false;
    for (const unsigned char character : std::string(text)) {
        if (!std::isspace(character)) return true;
    }
    return false;
}

std::string conversation_transcript(const std::vector<EditorAiChatMessage>& messages) {
    std::string transcript;
    for (const EditorAiChatMessage& message : messages) {
        if (!transcript.empty()) transcript += "\n\n";
        transcript += message.role == "user" ? "You\n" : "Codex\n";
        transcript += message.content;
    }
    return transcript;
}

std::unordered_map<EntityID, nlohmann::json> entity_map(const nlohmann::json& index) {
    std::unordered_map<EntityID, nlohmann::json> result;
    if (!index.is_array()) return result;
    for (const nlohmann::json& entity : index) {
        const EntityID id = entity.value("entity_id", EntityID{0});
        if (id != 0) result.emplace(id, entity);
    }
    return result;
}

nlohmann::json receipt_entity(const nlohmann::json& entity) {
    return {
        {"entity_id", entity.value("entity_id", EntityID{0})},
        {"name", entity.value("name", "")}
    };
}

nlohmann::json build_application_receipt(
    std::uint64_t request_id,
    std::uint64_t revision_before,
    std::uint64_t revision_after,
    const std::vector<EditorAiAuthoringAction>& actions,
    const nlohmann::json& before_index,
    const nlohmann::json& after_index
) {
    const auto before = entity_map(before_index);
    const auto after = entity_map(after_index);
    nlohmann::json added = nlohmann::json::array();
    nlohmann::json removed = nlohmann::json::array();
    nlohmann::json updated = nlohmann::json::array();
    constexpr std::size_t MAX_RECEIPT_ENTITIES = 100;
    std::size_t added_count = 0;
    std::size_t removed_count = 0;
    std::size_t updated_count = 0;

    for (const auto& [id, entity] : after) {
        const auto previous = before.find(id);
        if (previous == before.end()) {
            ++added_count;
            if (added.size() < MAX_RECEIPT_ENTITIES) added.push_back(receipt_entity(entity));
            continue;
        }
        nlohmann::json fields = nlohmann::json::array();
        for (const char* field : {"name", "parent_entity_id", "child_entity_ids", "transform", "components", "component_data"}) {
            if (previous->second.value(field, nlohmann::json{}) != entity.value(field, nlohmann::json{})) {
                fields.push_back(field);
            }
        }
        if (!fields.empty() && updated.size() < MAX_RECEIPT_ENTITIES) {
            ++updated_count;
            nlohmann::json item = receipt_entity(entity);
            item["fields"] = std::move(fields);
            updated.push_back(std::move(item));
        } else if (!fields.empty()) ++updated_count;
    }
    for (const auto& [id, entity] : before) {
        if (!after.contains(id) && removed.size() < MAX_RECEIPT_ENTITIES) {
            ++removed_count;
            removed.push_back(receipt_entity(entity));
        } else if (!after.contains(id)) ++removed_count;
    }

    nlohmann::json action_summaries = nlohmann::json::array();
    for (const EditorAiAuthoringAction& action : actions) action_summaries.push_back(action.summary);
    return {
        {"schema_version", 1},
        {"request_id", request_id},
        {"status", "applied"},
        {"verified", revision_after == revision_before + 1 &&
            (added_count + removed_count + updated_count) > 0},
        {"revision_before", revision_before},
        {"revision_after", revision_after},
        {"action_count", actions.size()},
        {"actions", std::move(action_summaries)},
        {"changes", {
            {"added", std::move(added)},
            {"removed", std::move(removed)},
            {"updated", std::move(updated)},
            {"added_count", added_count},
            {"removed_count", removed_count},
            {"updated_count", updated_count},
            {"truncated", added_count > MAX_RECEIPT_ENTITIES ||
                removed_count > MAX_RECEIPT_ENTITIES || updated_count > MAX_RECEIPT_ENTITIES},
            {"before_entity_count", before.size()},
            {"after_entity_count", after.size()}
        }}
    };
}

std::string receipt_summary(const nlohmann::json& receipt) {
    const auto& changes = receipt.at("changes");
    return std::string(receipt.value("verified", false)
        ? "Verified editor receipt: "
        : "Editor receipt (no serialized change observed): ") +
        std::to_string(changes.value("added_count", 0U)) + " added, " +
        std::to_string(changes.value("removed_count", 0U)) + " removed, " +
        std::to_string(changes.value("updated_count", 0U)) + " updated; revision " +
        std::to_string(receipt.value("revision_before", 0ULL)) + " -> " +
        std::to_string(receipt.value("revision_after", 0ULL)) + ".";
}

void draw_codex_badge() {
    constexpr ImVec2 PADDING(9.0f, 5.0f);
    const ImVec2 text_size = ImGui::CalcTextSize("Codex");
    const ImVec2 size(text_size.x + PADDING.x * 2.0f, text_size.y + PADDING.y * 2.0f);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        minimum,
        maximum,
        ImGui::GetColorU32(ImGuiCol_FrameBgHovered),
        7.0f
    );
    draw_list->AddText(
        ImVec2(minimum.x + PADDING.x, minimum.y + PADDING.y),
        IM_COL32(245, 245, 242, 255),
        "Codex"
    );
    ImGui::Dummy(size);
}

bool round_icon_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool enabled,
    bool emphasized
) {
    constexpr float DIAMETER = 32.0f;
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton("##button", ImVec2(DIAMETER, DIAMETER));
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const ImVec2 center(
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f
    );
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 background = !enabled
        ? IM_COL32(57, 62, 80, 145)
        : (emphasized
            ? (hovered ? IM_COL32(245, 245, 242, 255) : IM_COL32(218, 218, 213, 255))
            : (hovered ? IM_COL32(91, 82, 196, 170) : IM_COL32(72, 66, 145, 120)));
    const ImU32 foreground = !enabled
        ? IM_COL32(145, 145, 140, 150)
        : (emphasized ? IM_COL32(48, 49, 47, 255) : IM_COL32(225, 226, 222, 255));
    draw_list->AddCircleFilled(center, DIAMETER * 0.5f, background, 28);
    draw_editor_icon(draw_list, icon, center, 14.0f, foreground);
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return enabled && pressed;
}

std::string compact_model_name(const std::string& model) {
    if (model == "gpt-5.6-sol") return "5.6 Sol";
    if (model == "gpt-5.6-terra") return "5.6 Terra";
    if (model == "gpt-5.6-luna") return "5.6 Luna";
    return model;
}

bool draw_chat_message(
    const EditorAiChatMessage& message,
    int index,
    float available_width
) {
    bool copy_requested = false;
    const bool is_user = message.role == "user";
    const float bubble_width = std::max(
        140.0f,
        available_width * (is_user ? 0.82f : 0.94f)
    );
    if (is_user) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - bubble_width);
    }

    const ImVec4 background = is_user
        ? ImVec4(0.17f, 0.18f, 0.27f, 1.0f)
        : ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
    const ImVec4 label_color = is_user
        ? ImVec4(0.72f, 0.69f, 1.0f, 1.0f)
        : ImVec4(0.40f, 0.82f, 0.62f, 1.0f);
    const float text_width = std::max(1.0f, bubble_width - 20.0f);
    const float text_height = ImGui::CalcTextSize(
        message.content.c_str(),
        nullptr,
        false,
        text_width
    ).y;
    const float bubble_height = ImGui::GetTextLineHeightWithSpacing() + text_height + 20.0f;

    ImGui::PushID(index);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::BeginChild(
        "Message",
        ImVec2(bubble_width, bubble_height),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::TextColored(label_color, "%s", is_user ? "You" : "Codex");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_width);
    ImGui::TextUnformatted(message.content.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    if (!is_user) {
        if (editor_icon_button("copy", EditorIcon::Copy, "Copy response")) {
            copy_requested = true;
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Spacing();
    return copy_requested;
}

} // namespace

AiAssistantPanel::AiAssistantPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection,
    ComponentSerializationContext serialization_context
) : m_scene_manager(scene_manager),
    m_selection(std::move(selection)),
    m_serialization_context(serialization_context),
    m_tool_registry(build_component_probes(), project_root()),
    m_authoring_registry(&default_component_registry()),
    m_codex_provider(std::make_unique<CodexAppServerProvider>(
        create_default_codex_app_server_transport()
    )),
    m_openai_provider(std::make_unique<OpenAiResponsesProvider>(
        create_default_editor_ai_http_transport()
    )),
    m_conversation_store(std::make_unique<EditorAiConversationStore>(
        (std::filesystem::path(project_root()) / "Library/EditorAi/conversations.json").string()
    )),
    m_codex_executable(find_default_codex_executable()) {
    is_open = false;
    std::snprintf(m_model.data(), m_model.size(), "%s", "gpt-5.6-sol");
    std::string load_error;
    if (!m_conversation_store->load(load_error)) {
        ENGINE_LOG_WARN("[AI AUDIT] conversation_store=load_failed error={}", load_error);
    } else if (const auto* conversation = m_conversation_store->active_conversation()) {
        m_messages = conversation->messages;
        if (conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION) {
            m_codex_thread_id = conversation->thread_id;
            m_codex_session_id = conversation->session_id;
        } else {
            m_superseded_thread_id = conversation->thread_id;
            m_ui_feedback = "Editor tools changed; the next message will upgrade this conversation.";
        }
        if (!conversation->model.empty()) {
            std::snprintf(m_model.data(), m_model.size(), "%s", conversation->model.c_str());
        }
        m_scroll_chat_to_bottom = true;
        ENGINE_LOG_INFO(
            "[AI AUDIT] conversation_store=restored thread={} messages={} contract={} current_contract={} compatible={}",
            conversation->thread_id,
            conversation->messages.size(),
            conversation->tool_contract_version,
            EDITOR_AI_TOOL_CONTRACT_VERSION,
            conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION
        );
    }
}

AiAssistantPanel::~AiAssistantPanel() {
    cancel_request();
    if (m_request_thread.joinable()) m_request_thread.join();
}

std::string AiAssistantPanel::current_context_json() const {
    const Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    nlohmann::json context = m_tool_registry.build_context_snapshot(
        active_scene, m_selection.get(), m_serialization_context
    );
    context["authoring"] = {
        {"mode", m_authoring_mode == AuthoringMode::AutonomousSafe
            ? "autonomous_safe" : "confirm_before_apply"},
        {"filesystem", "read_only"},
        {"max_actions_per_proposal", EditorAiAuthoringRegistry::MAX_ACTIONS_PER_PROPOSAL},
        {"tools", m_authoring_registry.tool_schemas()}
    };
    return context.dump(2);
}

void AiAssistantPanel::cancel_request() {
    m_cancel_requested.store(true);
    ENGINE_LOG_INFO("[AI AUDIT] ui_action=cancel_request");
}

void AiAssistantPanel::start_new_conversation() {
    std::lock_guard lock(m_request_mutex);
    if (m_request_running || m_pending_proposal) return;
    m_messages.clear();
    m_streamed_response.clear();
    m_request_error.clear();
    m_active_tool.clear();
    m_authoring_feedback.clear();
    m_ui_feedback = "New conversation ready.";
    m_codex_thread_id.clear();
    m_codex_session_id.clear();
    m_superseded_thread_id.clear();
    m_last_input_tokens = 0;
    m_last_output_tokens = 0;
    m_last_total_tokens = 0;
    m_last_tool_calls = 0;
    m_last_request_duration_ms = 0;
    m_scroll_chat_to_bottom = false;
    m_focus_prompt_requested = true;
    m_prompt.fill('\0');
    if (m_conversation_store) {
        static_cast<void>(m_conversation_store->set_active_thread(""));
        std::string save_error;
        if (!m_conversation_store->save(save_error)) {
            m_request_error = save_error;
        }
    }
    ENGINE_LOG_INFO("[AI AUDIT] ui_action=new_conversation");
}

void AiAssistantPanel::restore_conversation(const std::string& thread_id) {
    std::lock_guard lock(m_request_mutex);
    if (m_request_running || m_pending_proposal || !m_conversation_store) return;
    const EditorAiConversationRecord* conversation = m_conversation_store->find(thread_id);
    if (!conversation) {
        m_request_error = "The selected conversation is no longer available.";
        return;
    }
    if (conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION) {
        m_codex_thread_id = conversation->thread_id;
        m_codex_session_id = conversation->session_id;
        m_superseded_thread_id.clear();
    } else {
        m_codex_thread_id.clear();
        m_codex_session_id.clear();
        m_superseded_thread_id = conversation->thread_id;
    }
    m_messages = conversation->messages;
    m_streamed_response.clear();
    m_request_error.clear();
    m_active_tool.clear();
    m_authoring_feedback.clear();
    m_ui_feedback = conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION
        ? "Conversation restored."
        : "Conversation restored; the next message will upgrade its editor tools.";
    m_last_input_tokens = 0;
    m_last_output_tokens = 0;
    m_last_total_tokens = 0;
    m_last_tool_calls = 0;
    m_last_request_duration_ms = 0;
    m_scroll_chat_to_bottom = true;
    m_focus_prompt_requested = true;
    m_prompt.fill('\0');
    if (!conversation->model.empty()) {
        std::snprintf(m_model.data(), m_model.size(), "%s", conversation->model.c_str());
    }
    static_cast<void>(m_conversation_store->set_active_thread(thread_id));
    std::string save_error;
    if (!m_conversation_store->save(save_error)) m_request_error = save_error;
    ENGINE_LOG_INFO(
        "[AI AUDIT] conversation_store=resumed thread={} messages={}",
        conversation->thread_id,
        conversation->messages.size()
    );
}

void AiAssistantPanel::rename_conversation(
    const std::string& thread_id,
    const std::string& title
) {
    std::lock_guard lock(m_request_mutex);
    if (!m_conversation_store || m_request_running || m_pending_proposal) return;
    if (!m_conversation_store->rename(thread_id, title)) {
        m_request_error = "Conversation could not be renamed. Enter a non-empty title.";
        return;
    }
    std::string save_error;
    if (!m_conversation_store->save(save_error)) {
        m_request_error = save_error;
        return;
    }
    m_request_error.clear();
    m_ui_feedback = "Conversation renamed.";
    ENGINE_LOG_INFO("[AI AUDIT] conversation_store=renamed thread={}", thread_id);
}

void AiAssistantPanel::delete_conversation(const std::string& thread_id) {
    std::lock_guard lock(m_request_mutex);
    if (!m_conversation_store || m_request_running || m_pending_proposal) return;
    if (!m_conversation_store->erase(thread_id)) {
        m_request_error = "The selected conversation is no longer available.";
        return;
    }
    if (m_codex_thread_id == thread_id || m_superseded_thread_id == thread_id) {
        m_messages.clear();
        m_streamed_response.clear();
        m_codex_thread_id.clear();
        m_codex_session_id.clear();
        m_superseded_thread_id.clear();
        m_last_input_tokens = 0;
        m_last_output_tokens = 0;
        m_last_total_tokens = 0;
        m_last_tool_calls = 0;
        m_last_request_duration_ms = 0;
        m_prompt.fill('\0');
        m_focus_prompt_requested = true;
    }
    std::string save_error;
    if (!m_conversation_store->save(save_error)) {
        m_request_error = save_error;
        return;
    }
    m_request_error.clear();
    m_ui_feedback = "Conversation deleted.";
    ENGINE_LOG_INFO("[AI AUDIT] conversation_store=deleted thread={}", thread_id);
}

void AiAssistantPanel::copy_to_clipboard(
    const std::string& contents,
    const std::string& feedback
) {
    if (contents.empty()) return;
    ImGui::SetClipboardText(contents.c_str());
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = feedback;
    ENGINE_LOG_INFO("[AI AUDIT] ui_action=clipboard_copy result={}", feedback);
}

void AiAssistantPanel::toggle_authoring_mode() {
    m_authoring_mode = m_authoring_mode == AuthoringMode::AutonomousSafe
        ? AuthoringMode::ConfirmBeforeApply
        : AuthoringMode::AutonomousSafe;
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = m_authoring_mode == AuthoringMode::AutonomousSafe
        ? "Autonomous safe mode enabled. Validated proposals will apply automatically."
        : "Confirmation enabled. Proposals will wait for your approval.";
    ENGINE_LOG_INFO(
        "[AI AUDIT] ui_action=authoring_mode mode={}",
        m_authoring_mode == AuthoringMode::AutonomousSafe
            ? "autonomous_safe" : "confirm_before_apply"
    );
}

void AiAssistantPanel::persist_current_conversation_locked(const std::string& model) {
    if (!m_conversation_store || m_codex_thread_id.empty()) return;
    const auto updated_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    m_conversation_store->record(EditorAiConversationRecord{
        .thread_id = m_codex_thread_id,
        .session_id = m_codex_session_id,
        .title = make_editor_ai_conversation_title(m_messages),
        .model = model,
        .tool_contract_version = EDITOR_AI_TOOL_CONTRACT_VERSION,
        .updated_at_unix_ms = updated_at,
        .messages = m_messages
    });
    if (!m_superseded_thread_id.empty() && m_superseded_thread_id != m_codex_thread_id) {
        const std::string previous_thread = m_superseded_thread_id;
        static_cast<void>(m_conversation_store->erase(previous_thread));
        m_superseded_thread_id.clear();
        ENGINE_LOG_INFO(
            "[AI AUDIT] conversation_store=contract_upgraded old_thread={} new_thread={} contract={}",
            previous_thread,
            m_codex_thread_id,
            EDITOR_AI_TOOL_CONTRACT_VERSION
        );
    }
    std::string save_error;
    if (!m_conversation_store->save(save_error)) {
        m_request_error = save_error;
        ENGINE_LOG_WARN(
            "[AI AUDIT] conversation_store=save_failed thread={} error={}",
            m_codex_thread_id,
            save_error
        );
    }
}

void AiAssistantPanel::dismiss_pending_proposal() {
    std::lock_guard lock(m_request_mutex);
    if (m_pending_proposal) {
        ENGINE_LOG_INFO(
            "[AI AUDIT] request={} proposal=dismissed actions={} scene={} baseline_revision={}",
            m_pending_proposal->request_id,
            m_pending_proposal->actions.size(),
            m_pending_proposal->scene_name,
            m_pending_proposal->baseline_revision
        );
    }
    m_pending_proposal.reset();
    m_authoring_feedback = "AI proposal dismissed without changing the scene.";
}

void AiAssistantPanel::apply_pending_proposal() {
    PendingProposal proposal;
    {
        std::lock_guard lock(m_request_mutex);
        if (!m_pending_proposal || m_request_running) return;
        proposal = *m_pending_proposal;
    }

    const auto reject = [this, request_id = proposal.request_id](std::string message) {
        ENGINE_LOG_WARN("[AI AUDIT] request={} proposal=rejected reason={}", request_id, message);
        std::lock_guard lock(m_request_mutex);
        m_pending_proposal.reset();
        m_authoring_feedback = "Proposal rejected: " + std::move(message);
    };

    const std::shared_ptr<Scene> scene = proposal.target_scene.lock();
    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    if (!scene || scene != active_scene) {
        reject("the active scene changed after the proposal was generated");
        return;
    }
    if (!m_scene_manager->is_editing()) {
        reject("authoring is available only in Edit mode");
        return;
    }
    if (!proposal.command_history || proposal.command_history != m_command_history ||
        !proposal.entity_state_store || proposal.entity_state_store != m_entity_state_store ||
        proposal.selection != m_selection) {
        reject("the editor session changed after the proposal was generated");
        return;
    }
    std::string error;
    std::string description;
    if (!m_authoring_registry.execute_proposal(
        proposal.actions,
        EditorAiAuthoringContext{
            .entities = &scene->get_entity_manager(),
            .selection = proposal.selection,
            .entity_state_store = proposal.entity_state_store,
            .component_registry = &default_component_registry(),
            .serialization = m_serialization_context
        },
        *proposal.command_history,
        proposal.baseline_revision,
        description,
        error
    )) {
        reject(error.empty() ? "the proposal could not be applied" : error);
        return;
    }

    const nlohmann::json after_snapshot = m_tool_registry.build_context_snapshot(
        scene.get(), proposal.selection.get(), m_serialization_context
    );
    const nlohmann::json receipt = build_application_receipt(
        proposal.request_id,
        proposal.baseline_revision,
        proposal.command_history->get_current_revision(),
        proposal.actions,
        proposal.before_entity_index,
        after_snapshot.value("entity_index", nlohmann::json::array())
    );
    const std::string summary = receipt_summary(receipt);

    std::lock_guard lock(m_request_mutex);
    m_pending_proposal.reset();
    m_last_application_receipt = receipt;
    m_last_receipt_summary = summary;
    m_authoring_feedback = description + " applied. " + summary + " One Undo restores the previous state.";
    ENGINE_LOG_INFO(
        "[AI AUDIT] request={} proposal=applied actions={} revision={} description={}",
        proposal.request_id,
        proposal.actions.size(),
        proposal.command_history->get_current_revision(),
        description
    );
    ENGINE_LOG_INFO("[AI AUDIT] receipt={}", receipt.dump());
    m_last_applied_session = AppliedAiSession{
        .request_id = proposal.request_id,
        .target_scene = scene,
        .command_history = proposal.command_history,
        .after_revision = proposal.command_history->get_current_revision(),
        .description = description
    };
}

void AiAssistantPanel::undo_last_ai_session() {
    AppliedAiSession session;
    {
        std::lock_guard lock(m_request_mutex);
        if (!m_last_applied_session) return;
        session = *m_last_applied_session;
    }
    const auto scene = session.target_scene.lock();
    const auto history = session.command_history.lock();
    const auto active_scene = m_scene_manager ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    if (!scene || scene != active_scene || !history || history != m_command_history ||
        history->get_current_revision() != session.after_revision ||
        history->get_undo_description() != session.description) {
        std::lock_guard lock(m_request_mutex);
        m_authoring_feedback = "AI session is no longer the latest scene edit; use the regular Undo history.";
        m_last_applied_session.reset();
        return;
    }
    history->undo();
    std::lock_guard lock(m_request_mutex);
    if (history->get_current_revision() != session.after_revision) {
        ENGINE_LOG_INFO(
            "[AI AUDIT] request={} proposal=undone description={}",
            session.request_id,
            session.description
        );
        m_authoring_feedback = session.description + " undone.";
        m_last_receipt_summary = "The previous AI editor session was undone; resolve the current scene again.";
        if (!m_last_application_receipt.empty()) {
            m_last_application_receipt["status"] = "undone";
            m_last_application_receipt["undo_revision"] = history->get_current_revision();
        }
        m_last_applied_session.reset();
    } else {
        m_authoring_feedback = "The AI session could not be undone; scene state was preserved.";
    }
}

void AiAssistantPanel::start_request() {
    const bool using_codex = m_backend == ProviderBackend::CodexSubscription;
    IEditorAiProvider* provider = using_codex
        ? static_cast<IEditorAiProvider*>(m_codex_provider.get())
        : static_cast<IEditorAiProvider*>(m_openai_provider.get());
    if (!provider || !provider->is_available()) return;
    const char* api_key_value = std::getenv("OPENAI_API_KEY");
    if (m_backend == ProviderBackend::OpenAiApi && (!api_key_value || api_key_value[0] == '\0')) {
        std::lock_guard lock(m_request_mutex);
        m_request_error = "Set OPENAI_API_KEY before launching the editor.";
        return;
    }
    if (m_backend == ProviderBackend::CodexSubscription && m_codex_executable.empty()) {
        std::lock_guard lock(m_request_mutex);
        m_request_error = "Codex CLI was not found. Install Codex or set CODEX_EXECUTABLE.";
        return;
    }

    const std::string prompt(m_prompt.data());
    const std::string model(m_model.data());
    if (!has_visible_text(prompt.c_str()) || !has_visible_text(model.c_str())) return;
    const std::uint64_t request_id = NEXT_AI_REQUEST_ID.fetch_add(1);
    const auto request_started = std::chrono::steady_clock::now();
    const std::string reasoning_effort = m_reasoning_effort == ReasoningEffort::High
        ? "high"
        : (m_reasoning_effort == ReasoningEffort::Medium ? "medium" : "low");

    {
        std::lock_guard lock(m_request_mutex);
        if (m_pending_proposal) {
            m_request_error = "Apply or dismiss the pending AI proposal before sending another request.";
            return;
        }
    }

    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    const std::shared_ptr<EditorCommandHistory> command_history = m_command_history;
    const std::shared_ptr<EditorEntityStateStore> entity_state_store = m_entity_state_store;
    const std::shared_ptr<EditorSelection> selection = m_selection;
    const bool authoring_available = active_scene && command_history && entity_state_store && selection &&
        m_scene_manager && m_scene_manager->is_editing();
    const std::uint64_t baseline_revision = command_history
        ? command_history->get_current_revision() : 0;
    const AuthoringMode authoring_mode = m_authoring_mode;
    auto proposal_actions = std::make_shared<std::vector<EditorAiAuthoringAction>>();
    const nlohmann::json frozen_context = m_tool_registry.build_context_snapshot(
        active_scene.get(), selection.get(), m_serialization_context
    );
    const nlohmann::json before_entity_index = frozen_context.value(
        "entity_index", nlohmann::json::array()
    );
    std::string previous_receipt;
    {
        std::lock_guard lock(m_request_mutex);
        previous_receipt = m_last_receipt_summary;
    }

    EditorAiProviderRequest request;
    request.api_key = api_key_value ? api_key_value : "";
    request.model = model;
    request.prompt = prompt;
    request.reasoning_effort = reasoning_effort;
    request.tools = m_tool_registry.tool_schemas();
    if (authoring_available) {
        for (const auto& schema : m_authoring_registry.tool_schemas()) {
            request.tools.push_back(schema);
        }
    }
    request.instructions = authoring_available
        ? "For an authoring request, act directly and do not narrate intermediate reasoning. The tool "
          "schemas are the source of truth: do not inspect workspace files to discover argument shapes. "
          "Use find_scene_entities before changing an existing target and pass its numeric entity_id. "
          "If a search returns multiple matches, narrow it or use selection context instead of guessing. "
          "entity_alias is request-local and may only reference an entity created earlier in the same "
          "proposal. On later conversation turns, resolve previously created entities again because aliases "
          "do not persist. Creating a root entity does not require inspection. Fold the final name, transform, and components of a new entity "
          "into one propose_create_entity call instead of queuing redundant follow-up actions. A successful "
          "queued result is final: do not wait for another editor response and do not retry it. If a call "
          "is invalid, correct it once from the returned error without reading files. Use find_project_assets "
          "for stable handles and prefer bulk duplicate/delete tools for sets. Queue at most 25 focused actions. "
          "New entities and prefab roots can be referenced by @alias. Reply with one concise sentence. "
          "The editor applies the proposal atomically through Undo history."
        : "Live authoring tools are unavailable for this request. Stay read-only and explain that the "
          "editor must be in Edit mode with an active scene before changes can be proposed.";
    if (authoring_available && authoring_mode == AuthoringMode::ConfirmBeforeApply) {
        request.instructions += " The user must confirm the proposal in the editor before it is applied.";
    } else if (authoring_available) {
        request.instructions += " Autonomous safe mode is enabled, so the editor will apply the proposal automatically if every validation gate passes.";
    }
    if (!previous_receipt.empty()) {
        request.instructions += " Previous editor result: " + previous_receipt;
    }
    request.project_root = project_root();
    request.codex_executable = m_codex_executable;
    request.persist_thread = using_codex;
    request.max_tool_rounds = 12;
    request.max_tool_failures = 3;
    {
        std::lock_guard lock(m_request_mutex);
        request.provider_thread_id = using_codex ? m_codex_thread_id : "";
        request.history = request.provider_thread_id.empty()
            ? recent_history(m_messages)
            : std::vector<EditorAiChatMessage>{};
        m_messages.push_back({"user", prompt});
        m_streamed_response.clear();
        m_request_error.clear();
        m_active_tool.clear();
        m_authoring_feedback.clear();
        m_ui_feedback.clear();
        m_last_input_tokens = 0;
        m_last_output_tokens = 0;
        m_last_total_tokens = 0;
        m_last_tool_calls = 0;
        m_last_request_duration_ms = 0;
        m_request_running = true;
        m_scroll_chat_to_bottom = true;
    }
    const std::size_t history_characters = std::accumulate(
        request.history.begin(),
        request.history.end(),
        std::size_t{0},
        [](std::size_t total, const EditorAiChatMessage& message) {
            return total + message.content.size();
        }
    );
    ENGINE_LOG_INFO(
        "[AI AUDIT] request={} phase=start backend={} model={} effort={} mode={} scene={} "
        "baseline_revision={} history_messages={} history_chars={} context_chars={} tools={} "
        "tool_schema_chars={} instructions_chars={} prompt_chars={} thread={} persistent={}",
        request_id,
        m_backend == ProviderBackend::CodexSubscription ? "codex_subscription" : "openai_api",
        model,
        reasoning_effort,
        authoring_mode == AuthoringMode::AutonomousSafe ? "autonomous_safe" : "confirm_before_apply",
        active_scene ? active_scene->get_name() : "none",
        baseline_revision,
        request.history.size(),
        history_characters,
        frozen_context.dump().size(),
        request.tools.size(),
        request.tools.dump().size(),
        request.instructions.size(),
        prompt.size(),
        request.provider_thread_id.empty() ? "new" : request.provider_thread_id,
        request.persist_thread
    );
    request.execute_tool = [
        this,
        frozen_context,
        proposal_actions,
        authoring_available,
        request_id,
        request_started
    ](
        const std::string& name,
        const nlohmann::json& arguments
    ) -> EditorAiToolResult {
        const auto tool_started = std::chrono::steady_clock::now();
        const auto audited = [&](EditorAiToolResult result) {
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tool_started
            ).count();
            ENGINE_LOG_INFO(
                "[AI AUDIT] request={} phase=tool name={} success={} elapsed_ms={} duration_ms={} "
                "queued_actions={} error={}",
                request_id,
                name,
                result.success,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - request_started
                ).count(),
                duration,
                proposal_actions->size(),
                result.success ? "none" : result.error
            );
            return result;
        };
        if (m_authoring_registry.is_authoring_tool(name)) {
            if (!authoring_available) {
                return audited({false, {}, "Authoring is unavailable for this request"});
            }
            return audited(m_authoring_registry.queue_action(
                name, arguments, *proposal_actions, &frozen_context
            ));
        }
        return audited(m_tool_registry.execute_snapshot(name, arguments, frozen_context));
    };
    m_prompt.fill('\0');
    if (m_request_thread.joinable()) m_request_thread.join();
    m_cancel_requested.store(false);

    m_request_thread = std::thread([
        this,
        provider,
        request = std::move(request),
        proposal_actions,
        active_scene,
        command_history,
        entity_state_store,
        selection,
        baseline_revision,
        before_entity_index,
        authoring_mode,
        using_codex,
        model,
        request_id,
        request_started
    ]() mutable {
        bool first_text_logged = false;
        const EditorAiProviderResult result = provider->run(
            request,
            [this, request_id, request_started, &first_text_logged](const EditorAiProviderEvent& event) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - request_started
                ).count();
                if (event.type == EditorAiProviderEventType::Started) {
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=provider_started elapsed_ms={}",
                        request_id,
                        elapsed
                    );
                } else if (event.type == EditorAiProviderEventType::TextDelta && !first_text_logged) {
                    first_text_logged = true;
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=first_text elapsed_ms={}",
                        request_id,
                        elapsed
                    );
                } else if (event.type == EditorAiProviderEventType::ToolStarted) {
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=tool_started name={} elapsed_ms={}",
                        request_id,
                        event.tool_name,
                        elapsed
                    );
                }
                std::lock_guard lock(m_request_mutex);
                if (event.type == EditorAiProviderEventType::TextDelta) {
                    m_streamed_response += event.text;
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ToolStarted) {
                    m_active_tool = event.tool_name;
                } else if (event.type == EditorAiProviderEventType::ToolFinished) {
                    m_active_tool.clear();
                }
            },
            [this]() { return m_cancel_requested.load(); }
        );

        std::lock_guard lock(m_request_mutex);
        m_request_running = false;
        m_active_tool.clear();
        m_last_input_tokens = result.input_tokens;
        m_last_output_tokens = result.output_tokens;
        m_last_total_tokens = result.total_tokens;
        m_last_tool_calls = result.tool_call_count;
        if (!result.provider_thread_id.empty()) {
            m_codex_thread_id = result.provider_thread_id;
        }
        if (!result.provider_session_id.empty()) {
            m_codex_session_id = result.provider_session_id;
        }
        m_scroll_chat_to_bottom = true;
        m_last_request_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - request_started
            ).count()
        );
        const char* status = result.status == EditorAiProviderStatus::Completed
            ? "completed"
            : (result.status == EditorAiProviderStatus::Cancelled ? "cancelled" : "failed");
        ENGINE_LOG_INFO(
            "[AI AUDIT] request={} phase=finish status={} duration_ms={} tool_calls={} "
            "proposal_actions={} input_tokens={} output_tokens={} total_tokens={} thread={} "
            "session={} error={}",
            request_id,
            status,
            m_last_request_duration_ms,
            result.tool_call_count,
            proposal_actions->size(),
            result.input_tokens,
            result.output_tokens,
            result.total_tokens,
            result.provider_thread_id.empty() ? "none" : result.provider_thread_id,
            result.provider_session_id.empty() ? "none" : result.provider_session_id,
            result.error.empty() ? "none" : result.error
        );
        if (result.status == EditorAiProviderStatus::Completed) {
            if (m_streamed_response.empty()) m_streamed_response = result.text;
            m_messages.push_back({"assistant", m_streamed_response});
            if (!proposal_actions->empty()) {
                m_pending_proposal = PendingProposal{
                    .request_id = request_id,
                    .actions = std::move(*proposal_actions),
                    .target_scene = active_scene,
                    .command_history = command_history,
                    .entity_state_store = entity_state_store,
                    .selection = selection,
                    .baseline_revision = baseline_revision,
                    .scene_name = active_scene ? active_scene->get_name() : "",
                    .before_entity_index = before_entity_index,
                    .auto_apply = authoring_mode == AuthoringMode::AutonomousSafe
                };
                m_authoring_feedback = m_pending_proposal->auto_apply
                    ? "Proposal validated by the tool boundary; applying on the editor thread..."
                    : "Proposal ready for review. The scene has not changed yet.";
            }
        } else if (result.status == EditorAiProviderStatus::Cancelled) {
            m_request_error = "Request cancelled.";
            if (!proposal_actions->empty()) {
                m_authoring_feedback = "Cancelled proposal discarded without changing the scene.";
            }
        } else {
            m_request_error = result.error;
            if (!proposal_actions->empty()) {
                m_authoring_feedback = "Failed proposal discarded without changing the scene.";
            }
        }
        if (using_codex && result.status == EditorAiProviderStatus::Completed &&
            !m_codex_thread_id.empty()) {
            persist_current_conversation_locked(model);
        }
    });
}

void AiAssistantPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(500.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (m_focus_requested) {
        ImGui::SetNextWindowFocus();
        m_focus_requested = false;
    }
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    bool auto_apply_proposal = false;
    {
        std::lock_guard lock(m_request_mutex);
        auto_apply_proposal = !m_request_running && m_pending_proposal &&
            m_pending_proposal->auto_apply;
    }
    if (auto_apply_proposal) apply_pending_proposal();

    const bool codex_available = m_codex_provider && m_codex_provider->is_available() &&
        !m_codex_executable.empty();
    const bool api_provider_available = m_openai_provider && m_openai_provider->is_available();
    const char* api_key = std::getenv("OPENAI_API_KEY");
    const bool api_key_available = api_key && api_key[0] != '\0';
    const bool using_codex = m_backend == ProviderBackend::CodexSubscription;
    const bool provider_available = using_codex ? codex_available : api_provider_available;
    const bool credentials_available = using_codex || api_key_available;
    bool running = false;
    std::string streamed_response;
    std::string request_error;
    std::string active_tool;
    std::vector<EditorAiChatMessage> messages;
    std::size_t input_tokens = 0;
    std::size_t output_tokens = 0;
    std::size_t total_tokens = 0;
    std::size_t tool_calls = 0;
    std::uint64_t request_duration_ms = 0;
    std::optional<PendingProposal> pending_proposal;
    std::optional<AppliedAiSession> last_applied_session;
    std::string authoring_feedback;
    std::string ui_feedback;
    nlohmann::json application_receipt;
    std::string codex_thread_id;
    std::string codex_session_id;
    std::vector<EditorAiConversationRecord> conversations;
    bool scroll_chat_to_bottom = false;
    bool open_rename_conversation = false;
    bool open_delete_conversation = false;
    {
        std::lock_guard lock(m_request_mutex);
        running = m_request_running;
        streamed_response = m_streamed_response;
        request_error = m_request_error;
        active_tool = m_active_tool;
        messages = m_messages;
        input_tokens = m_last_input_tokens;
        output_tokens = m_last_output_tokens;
        total_tokens = m_last_total_tokens;
        tool_calls = m_last_tool_calls;
        request_duration_ms = m_last_request_duration_ms;
        pending_proposal = m_pending_proposal;
        last_applied_session = m_last_applied_session;
        authoring_feedback = m_authoring_feedback;
        ui_feedback = m_ui_feedback;
        application_receipt = m_last_application_receipt;
        codex_thread_id = m_codex_thread_id;
        codex_session_id = m_codex_session_id;
        if (m_conversation_store) conversations = m_conversation_store->conversations();
        scroll_chat_to_bottom = m_scroll_chat_to_bottom;
        m_scroll_chat_to_bottom = false;
    }

    std::string conversation_title = "New conversation";
    const auto current_conversation = std::find_if(
        conversations.begin(),
        conversations.end(),
        [&](const EditorAiConversationRecord& conversation) {
            return conversation.thread_id == codex_thread_id;
        }
    );
    if (current_conversation != conversations.end()) {
        conversation_title = current_conversation->title;
    }

    draw_codex_badge();
    if (m_open_session_settings_requested) {
        ImGui::OpenPopup("AISessionSettings");
        m_open_session_settings_requested = false;
    }
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));
    constexpr float TOOLBAR_WIDTH = 28.0f * 4.0f + 5.0f * 3.0f;
    const float toolbar_x = ImGui::GetCursorPosX() + std::max(
        0.0f,
        ImGui::GetContentRegionAvail().x - TOOLBAR_WIDTH
    );
    ImGui::SetCursorPosX(toolbar_x);
    if (editor_icon_button(
        "conversation_actions",
        EditorIcon::Ellipsis,
        "Conversation actions",
        ImGui::IsPopupOpen("AIConversationActions"),
        ImVec2(28.0f, 28.0f)
    )) {
        ImGui::OpenPopup("AIConversationActions");
    }
    ImGui::SameLine();
    if (editor_icon_button(
        "conversation_history",
        EditorIcon::History,
        "Conversation history",
        ImGui::IsPopupOpen("AIConversationHistory"),
        ImVec2(28.0f, 28.0f)
    )) {
        ImGui::OpenPopup("AIConversationHistory");
    }
    ImGui::SameLine();
    if (editor_icon_button(
        "session_settings",
        EditorIcon::Settings,
        "Session settings",
        ImGui::IsPopupOpen("AISessionSettings"),
        ImVec2(28.0f, 28.0f)
    )) {
        ImGui::OpenPopup("AISessionSettings");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    if (editor_icon_button(
        "new_conversation",
        EditorIcon::Edit,
        "New conversation",
        codex_thread_id.empty() && messages.empty(),
        ImVec2(28.0f, 28.0f)
    )) {
        start_new_conversation();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(520.0f, 460.0f));
    if (ImGui::BeginPopup("AIConversationHistory")) {
        ImGui::TextUnformatted("Conversations");
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        if (ImGui::Selectable("New conversation", codex_thread_id.empty())) {
            start_new_conversation();
        }
        if (conversations.empty()) {
            ImGui::TextDisabled("No saved conversations yet.");
        }
        for (const EditorAiConversationRecord& conversation : conversations) {
            const std::string label = conversation.title + "##" + conversation.thread_id;
            if (ImGui::Selectable(
                label.c_str(),
                conversation.thread_id == codex_thread_id
            )) {
                restore_conversation(conversation.thread_id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s\n%zu messages",
                    conversation.model.c_str(),
                    conversation.thread_id.c_str(),
                    conversation.messages.size()
                );
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("AISessionSettings")) {
        ImGui::TextUnformatted("Session settings");
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        const char* backends[] = {"Codex subscription", "OpenAI API key"};
        int backend_index = m_backend == ProviderBackend::CodexSubscription ? 0 : 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Backend", &backend_index, backends, 2)) {
            m_backend = backend_index == 0
                ? ProviderBackend::CodexSubscription
                : ProviderBackend::OpenAiApi;
            start_new_conversation();
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Backend changed. A new conversation was started.";
        }
        if (m_backend == ProviderBackend::CodexSubscription) {
            ImGui::TextDisabled(
                "Sandbox: engine read-only; game authoring requires validated editor tools"
            );
        }
        const std::string current_model(m_model.data());
        int model_preset = current_model == "gpt-5.6-luna" ? 0
            : (current_model == "gpt-5.6-terra" ? 1
            : (current_model == "gpt-5.6-sol" ? 2 : 3));
        const char* model_presets[] = {
            "Luna - fastest",
            "Terra - balanced",
            "Sol - highest capability",
            "Custom"
        };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Model preset", &model_preset, model_presets, 4) && model_preset < 3) {
            const char* selected_model = model_preset == 0
                ? "gpt-5.6-luna" : (model_preset == 1 ? "gpt-5.6-terra" : "gpt-5.6-sol");
            std::snprintf(m_model.data(), m_model.size(), "%s", selected_model);
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Model updated.";
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Model", m_model.data(), m_model.size())) {
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Model updated.";
        }
        const char* reasoning_efforts[] = {"Low - fast", "Medium", "High"};
        int effort_index = m_reasoning_effort == ReasoningEffort::Low ? 0
            : (m_reasoning_effort == ReasoningEffort::Medium ? 1 : 2);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Reasoning", &effort_index, reasoning_efforts, 3)) {
            m_reasoning_effort = effort_index == 0
                ? ReasoningEffort::Low
                : (effort_index == 1 ? ReasoningEffort::Medium : ReasoningEffort::High);
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Reasoning effort updated.";
        }
        const char* authoring_modes[] = {"Confirm before apply", "Autonomous safe"};
        int authoring_mode_index = m_authoring_mode == AuthoringMode::ConfirmBeforeApply ? 0 : 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Authoring", &authoring_mode_index, authoring_modes, 2)) {
            m_authoring_mode = authoring_mode_index == 0
                ? AuthoringMode::ConfirmBeforeApply
                : AuthoringMode::AutonomousSafe;
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = m_authoring_mode == AuthoringMode::AutonomousSafe
                ? "Autonomous safe mode enabled."
                : "Changes now require confirmation.";
        }
        ImGui::Separator();
        if (ImGui::Button("Reset defaults")) {
            m_backend = ProviderBackend::CodexSubscription;
            m_reasoning_effort = ReasoningEffort::Low;
            m_authoring_mode = AuthoringMode::ConfirmBeforeApply;
            std::snprintf(m_model.data(), m_model.size(), "%s", "gpt-5.6-sol");
            start_new_conversation();
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Session defaults restored.";
            ENGINE_LOG_INFO("[AI AUDIT] ui_action=session_settings_reset");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AIConversationActions")) {
        ImGui::BeginDisabled(messages.empty());
        if (ImGui::MenuItem("Copy conversation")) {
            copy_to_clipboard(conversation_transcript(messages), "Conversation copied.");
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::BeginDisabled(codex_thread_id.empty());
        if (ImGui::MenuItem("Copy thread ID")) {
            copy_to_clipboard(codex_thread_id, "Thread ID copied.");
        }
        if (ImGui::MenuItem("Copy resume command")) {
            copy_to_clipboard(
                codex_resume_command(codex_thread_id),
                "Codex resume command copied."
            );
        }
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        if (ImGui::MenuItem("Rename conversation...")) {
            m_conversation_action_thread_id = codex_thread_id;
            std::snprintf(
                m_conversation_title.data(),
                m_conversation_title.size(),
                "%s",
                conversation_title.c_str()
            );
            open_rename_conversation = true;
        }
        if (ImGui::MenuItem("Delete conversation...")) {
            m_conversation_action_thread_id = codex_thread_id;
            open_delete_conversation = true;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (codex_thread_id.empty()) ImGui::TextDisabled("No saved conversation selected.");
        ImGui::Separator();
        ImGui::TextDisabled("History: Library/EditorAi");
        ImGui::TextDisabled("Audit: engine.log");
        ImGui::EndPopup();
    }

    if (open_rename_conversation) ImGui::OpenPopup("Rename AI conversation");
    if (open_delete_conversation) ImGui::OpenPopup("Delete AI conversation");

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
        "Rename AI conversation",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize
    )) {
        ImGui::TextUnformatted("Conversation title");
        ImGui::SetNextItemWidth(-1.0f);
        const bool submit_rename = ImGui::InputText(
            "##conversation_title",
            m_conversation_title.data(),
            m_conversation_title.size(),
            ImGuiInputTextFlags_EnterReturnsTrue
        );
        const bool valid_title = has_visible_text(m_conversation_title.data());
        ImGui::BeginDisabled(!valid_title);
        if (ImGui::Button("Rename") || (submit_rename && valid_title)) {
            rename_conversation(m_conversation_action_thread_id, m_conversation_title.data());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
        "Delete AI conversation",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize
    )) {
        ImGui::TextWrapped(
            "Delete '%s' from local editor history? This does not delete the remote Codex thread.",
            conversation_title.c_str()
        );
        if (ImGui::Button("Delete")) {
            delete_conversation(m_conversation_action_thread_id);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::TextWrapped("%s", conversation_title.c_str());
    if (provider_available && credentials_available) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Connected");
    } else if (using_codex) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.32f, 1.0f), "Codex CLI not found");
    } else if (!provider_available) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.32f, 1.0f), "HTTP unavailable");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.32f, 1.0f), "API key missing");
    }
    if (!codex_session_id.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Session %s", abbreviated_id(codex_session_id).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", codex_session_id.c_str());
        }
    }
    ImGui::Separator();

    const float conversation_height = std::clamp(
        ImGui::GetContentRegionAvail().y - 270.0f,
        220.0f,
        760.0f
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("AIConversation", ImVec2(0.0f, conversation_height), true);
    if (messages.empty() && streamed_response.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Start a conversation");
        ImGui::TextWrapped(
            "Ask about the active scene, the selected entity, renderer contracts, or request a "
            "reversible scene change."
        );
    }
    int message_index = 0;
    for (const EditorAiChatMessage& message : messages) {
        if (draw_chat_message(message, message_index++, ImGui::GetContentRegionAvail().x)) {
            copy_to_clipboard(message.content, "Response copied.");
        }
    }
    if (running && !streamed_response.empty()) {
        if (draw_chat_message(
            EditorAiChatMessage{"assistant", streamed_response},
            message_index,
            ImGui::GetContentRegionAvail().x
        )) {
            copy_to_clipboard(streamed_response, "Partial response copied.");
        }
    }
    if (running && streamed_response.empty()) {
        ImGui::TextColored(ImVec4(0.40f, 0.82f, 0.62f, 1.0f), "Codex is working...");
    }
    if (scroll_chat_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    constexpr float COMPOSER_HEIGHT = 126.0f;
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImGui::GetStyleColorVec4(ImGuiCol_Border)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13.0f, 11.0f));
    ImGui::BeginChild("AIComposer", ImVec2(0.0f, COMPOSER_HEIGHT), true);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    if (m_focus_prompt_requested) {
        ImGui::SetKeyboardFocusHere();
        m_focus_prompt_requested = false;
    }
    const bool submitted = ImGui::InputTextMultiline(
        "##ai_prompt",
        m_prompt.data(),
        m_prompt.size(),
        ImVec2(-1.0f, 62.0f),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine
    );
    ImGui::EndDisabled();
    const ImVec2 prompt_min = ImGui::GetItemRectMin();
    if (m_prompt[0] == '\0' && !ImGui::IsItemActive()) {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(prompt_min.x + 1.0f, prompt_min.y + 1.0f),
            IM_COL32(185, 185, 180, 175),
            "Ask Codex about the scene or engine..."
        );
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    const bool can_send = provider_available && credentials_available && !running &&
        !pending_proposal.has_value() &&
        has_visible_text(m_prompt.data()) && has_visible_text(m_model.data());
    if (submitted && can_send) start_request();

    if (editor_icon_button(
        "composer_actions",
        EditorIcon::Plus,
        "Add context or start a new conversation",
        ImGui::IsPopupOpen("AIComposerActions"),
        ImVec2(24.0f, 24.0f)
    )) {
        ImGui::OpenPopup("AIComposerActions");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    if (editor_icon_button(
        "authoring_security",
        EditorIcon::Shield,
        m_authoring_mode == AuthoringMode::AutonomousSafe
            ? "Autonomous safe: validated changes apply automatically"
            : "Confirm before apply: review every scene change",
        m_authoring_mode == AuthoringMode::AutonomousSafe,
        ImVec2(24.0f, 24.0f)
    )) {
        toggle_authoring_mode();
    }
    ImGui::EndDisabled();
    if (ImGui::GetContentRegionAvail().x > 330.0f) {
        ImGui::SameLine();
        if (m_authoring_mode == AuthoringMode::AutonomousSafe) {
            ImGui::TextColored(
                ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline),
                "Autonomous safe"
            );
        } else {
            ImGui::TextDisabled("Confirm changes");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change authoring approval mode");
        if (ImGui::IsItemClicked() && !running && !pending_proposal.has_value()) {
            toggle_authoring_mode();
        }
    }

    const std::string effort_label = m_reasoning_effort == ReasoningEffort::Low
        ? "Low" : (m_reasoning_effort == ReasoningEffort::Medium ? "Medium" : "High");
    const std::string model_label = compact_model_name(m_model.data()) + "  " + effort_label;
    const float right_controls_width = ImGui::CalcTextSize(model_label.c_str()).x + 44.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - right_controls_width
    ));
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    ImGui::TextDisabled("%s", model_label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open model and reasoning settings");
    if (ImGui::IsItemClicked()) m_open_session_settings_requested = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (running) {
        if (round_icon_button("cancel", EditorIcon::Stop, "Cancel request", true, false)) {
            cancel_request();
        }
    } else if (round_icon_button("send", EditorIcon::ArrowUp, "Send", can_send, true)) {
        start_request();
    }

    if (ImGui::BeginPopup("AIComposerActions")) {
        if (ImGui::MenuItem("Copy scene context")) {
            copy_to_clipboard(current_context_json(), "Scene context copied.");
        }
        if (ImGui::MenuItem("Copy selection context")) {
            const nlohmann::json context = nlohmann::json::parse(
                current_context_json(), nullptr, false
            );
            const std::string selection_context = !context.is_discarded() &&
                    context.contains("selection")
                ? context.at("selection").dump(2)
                : "{}";
            copy_to_clipboard(selection_context, "Selection context copied.");
        }
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        ImGui::BeginDisabled(m_prompt[0] == '\0');
        if (ImGui::MenuItem("Clear message")) {
            m_prompt.fill('\0');
            m_focus_prompt_requested = true;
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Message cleared.";
        }
        ImGui::EndDisabled();
        if (ImGui::MenuItem("New conversation")) start_new_conversation();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    if (!active_tool.empty()) {
        ImGui::TextDisabled("Working: %s", active_tool.c_str());
    }
    if (!request_error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.32f, 1.0f), "%s", request_error.c_str());
    }
    if (!running && total_tokens > 0) {
        ImGui::TextDisabled(
            "Last request: %.2fs | %zu tools",
            static_cast<double>(request_duration_ms) / 1000.0,
            tool_calls
        );
        ImGui::TextDisabled(
            "Usage: %zu input | %zu output | %zu total tokens",
            input_tokens,
            output_tokens,
            total_tokens
        );
    }
    ImGui::TextDisabled("Audit logs: Console + engine.log");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Request IDs and session metadata only; no prompts or credentials");
    }
    if (!ui_feedback.empty()) {
        ImGui::TextColored(ImVec4(0.48f, 0.66f, 0.95f, 1.0f), "%s", ui_feedback.c_str());
    }

    if (pending_proposal) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Pending AI proposal");
        ImGui::TextDisabled(
            "%zu actions | scene: %s | baseline revision: %llu",
            pending_proposal->actions.size(),
            pending_proposal->scene_name.c_str(),
            static_cast<unsigned long long>(pending_proposal->baseline_revision)
        );
        ImGui::BeginChild("AIPendingProposal", ImVec2(0.0f, 118.0f), true);
        for (const EditorAiAuthoringAction& action : pending_proposal->actions) {
            ImGui::BulletText("%s", action.summary.c_str());
        }
        ImGui::EndChild();
        if (!pending_proposal->auto_apply) {
            if (ImGui::Button("Apply proposal")) apply_pending_proposal();
            ImGui::SameLine();
            if (ImGui::Button("Dismiss")) dismiss_pending_proposal();
        }
    }

    if (!authoring_feedback.empty()) {
        const bool failed = authoring_feedback.starts_with("Proposal rejected") ||
            authoring_feedback.starts_with("Failed");
        ImGui::TextColored(
            failed ? ImVec4(0.95f, 0.36f, 0.32f, 1.0f)
                   : ImVec4(0.35f, 0.78f, 0.52f, 1.0f),
            "%s",
            authoring_feedback.c_str()
        );
    }
    if (!application_receipt.empty() && ImGui::CollapsingHeader("Last application receipt")) {
        const std::string receipt_text = application_receipt.dump(2);
        if (ImGui::Button("Copy receipt JSON")) {
            copy_to_clipboard(receipt_text, "Application receipt copied.");
        }
        ImGui::BeginChild("AIApplicationReceipt", ImVec2(0.0f, 150.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(receipt_text.c_str());
        ImGui::EndChild();
    }

    bool can_undo_ai_session = false;
    if (last_applied_session) {
        const auto session_scene = last_applied_session->target_scene.lock();
        const auto session_history = last_applied_session->command_history.lock();
        const auto active_scene = m_scene_manager
            ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
        can_undo_ai_session = session_scene && session_scene == active_scene && session_history &&
            session_history == m_command_history &&
            session_history->get_current_revision() == last_applied_session->after_revision &&
            session_history->get_undo_description() == last_applied_session->description;
    }
    if (last_applied_session) {
        ImGui::BeginDisabled(!can_undo_ai_session);
        if (ImGui::Button("Undo last AI session")) undo_last_ai_session();
        ImGui::EndDisabled();
        if (!can_undo_ai_session) {
            ImGui::SameLine();
            ImGui::TextDisabled("Use regular Undo: newer edits exist.");
        }
    }

    if (ImGui::CollapsingHeader("Context preview")) {
        const std::string context = current_context_json();
        if (ImGui::Button("Copy JSON")) {
            copy_to_clipboard(context, "Context JSON copied.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Live scene metadata; document contents are loaded only on request");
        ImGui::BeginChild("AIContextPreview", ImVec2(0.0f, 180.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(context.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace Engine

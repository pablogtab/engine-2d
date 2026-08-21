#include "AiAssistantPanel.hpp"

#include "../../scene/ComponentRegistry.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../core/Log.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../state/EditorEntityStateStore.hpp"

#include <imgui.h>

#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <numeric>

namespace Engine {
namespace {

std::atomic_uint64_t NEXT_AI_REQUEST_ID{1};

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
        probes.push_back({descriptor.serialized_key, descriptor.has});
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
    m_codex_executable(find_default_codex_executable()) {
    is_open = false;
    std::snprintf(m_model.data(), m_model.size(), "%s", "gpt-5.6-sol");
}

AiAssistantPanel::~AiAssistantPanel() {
    cancel_request();
    if (m_request_thread.joinable()) m_request_thread.join();
}

std::string AiAssistantPanel::current_context_json() const {
    const Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    nlohmann::json context = m_tool_registry.build_context_snapshot(active_scene, m_selection.get());
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

    std::lock_guard lock(m_request_mutex);
    m_pending_proposal.reset();
    m_authoring_feedback = description + " applied. One Undo restores the previous state.";
    ENGINE_LOG_INFO(
        "[AI AUDIT] request={} proposal=applied actions={} revision={} description={}",
        proposal.request_id,
        proposal.actions.size(),
        proposal.command_history->get_current_revision(),
        description
    );
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
        m_last_applied_session.reset();
    } else {
        m_authoring_feedback = "The AI session could not be undone; scene state was preserved.";
    }
}

void AiAssistantPanel::start_request() {
    IEditorAiProvider* provider = m_backend == ProviderBackend::CodexSubscription
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
    if (prompt.empty() || model.empty()) return;
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
        active_scene.get(), selection.get()
    );

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
          "Use get_scene_summary only when an existing target must be identified; creating a root entity "
          "does not require inspection. Fold the final name, transform, and components of a new entity "
          "into one propose_create_entity call instead of queuing redundant follow-up actions. A successful "
          "queued result is final: do not wait for another editor response and do not retry it. If a call "
          "is invalid, correct it once from the returned error without reading files. Queue at most 10 "
          "focused actions. New entities can be referenced by @alias. Reply with one concise sentence. "
          "The editor applies the proposal atomically through Undo history."
        : "Live authoring tools are unavailable for this request. Stay read-only and explain that the "
          "editor must be in Edit mode with an active scene before changes can be proposed.";
    if (authoring_available && authoring_mode == AuthoringMode::ConfirmBeforeApply) {
        request.instructions += " The user must confirm the proposal in the editor before it is applied.";
    } else if (authoring_available) {
        request.instructions += " Autonomous safe mode is enabled, so the editor will apply the proposal automatically if every validation gate passes.";
    }
    request.project_root = project_root();
    request.codex_executable = m_codex_executable;
    request.max_tool_rounds = 12;
    request.max_tool_failures = 3;
    {
        std::lock_guard lock(m_request_mutex);
        request.history = recent_history(m_messages);
        m_messages.push_back({"user", prompt});
        m_streamed_response.clear();
        m_request_error.clear();
        m_active_tool.clear();
        m_authoring_feedback.clear();
        m_last_input_tokens = 0;
        m_last_output_tokens = 0;
        m_last_total_tokens = 0;
        m_last_tool_calls = 0;
        m_last_request_duration_ms = 0;
        m_request_running = true;
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
        "tool_schema_chars={} instructions_chars={} prompt_chars={}",
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
        prompt.size()
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
            return audited(m_authoring_registry.queue_action(name, arguments, *proposal_actions));
        }
        if (name == "get_scene_summary" || name == "get_selection_context") {
            if (!arguments.is_object() || !arguments.empty()) {
                return audited({false, {}, "Tool arguments must be an empty object"});
            }
            const char* key = name == "get_scene_summary" ? "scene" : "selection";
            if (!frozen_context.contains(key) || frozen_context.at(key).contains("error")) {
                const std::string error = frozen_context.contains(key)
                    ? frozen_context.at(key).value("error", "Context is unavailable")
                    : "Context is unavailable";
                return audited({false, {}, error});
            }
            return audited({true, frozen_context.at(key), {}});
        }
        return audited(m_tool_registry.execute(name, arguments, nullptr, nullptr));
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
        authoring_mode,
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
            "proposal_actions={} input_tokens={} output_tokens={} total_tokens={} error={}",
            request_id,
            status,
            m_last_request_duration_ms,
            result.tool_call_count,
            proposal_actions->size(),
            result.input_tokens,
            result.output_tokens,
            result.total_tokens,
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
    });
}

void AiAssistantPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(500.0f, 680.0f), ImGuiCond_FirstUseEver);
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
    ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Local tools ready");
    ImGui::SameLine();
    if (using_codex && codex_available) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Codex subscription ready");
    } else if (!using_codex && provider_available && api_key_available) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "OpenAI API ready");
    } else if (using_codex) {
        ImGui::TextDisabled("Codex CLI not found");
    } else if (!provider_available) {
        ImGui::TextDisabled("HTTP transport unavailable in this build");
    } else {
        ImGui::TextDisabled("OPENAI_API_KEY not configured");
    }
    ImGui::TextWrapped(
        "Codex receives the active scene, selection, component registry, and engine contracts through "
        "typed editor tools. The engine workspace stays read-only. Scene changes are queued through "
        "validated authoring tools and applied as one reversible editor transaction."
    );

    ImGui::Spacing();
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
    }

    ImGui::BeginDisabled(running || pending_proposal.has_value());
    const char* backends[] = {"Codex subscription", "OpenAI API key"};
    int backend_index = m_backend == ProviderBackend::CodexSubscription ? 0 : 1;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Backend", &backend_index, backends, 2)) {
        m_backend = backend_index == 0
            ? ProviderBackend::CodexSubscription
            : ProviderBackend::OpenAiApi;
    }
    if (m_backend == ProviderBackend::CodexSubscription) {
        ImGui::TextDisabled("Sandbox: engine read-only; game authoring requires validated editor tools");
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
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Model", m_model.data(), m_model.size());
    const char* reasoning_efforts[] = {"Low - fast", "Medium", "High"};
    int effort_index = m_reasoning_effort == ReasoningEffort::Low ? 0
        : (m_reasoning_effort == ReasoningEffort::Medium ? 1 : 2);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Reasoning", &effort_index, reasoning_efforts, 3)) {
        m_reasoning_effort = effort_index == 0
            ? ReasoningEffort::Low
            : (effort_index == 1 ? ReasoningEffort::Medium : ReasoningEffort::High);
    }
    const char* authoring_modes[] = {"Confirm before apply", "Autonomous safe"};
    int authoring_mode_index = m_authoring_mode == AuthoringMode::ConfirmBeforeApply ? 0 : 1;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Authoring", &authoring_mode_index, authoring_modes, 2)) {
        m_authoring_mode = authoring_mode_index == 0
            ? AuthoringMode::ConfirmBeforeApply
            : AuthoringMode::AutonomousSafe;
    }
    ImGui::EndDisabled();
    if (m_authoring_mode == AuthoringMode::ConfirmBeforeApply) {
        ImGui::TextDisabled("Review every proposed action before the scene changes.");
    } else {
        ImGui::TextDisabled("Validated proposals apply automatically; one Undo reverts the whole session.");
    }

    ImGui::BeginChild("AIConversation", ImVec2(0.0f, 180.0f), true);
    if (messages.empty() && streamed_response.empty()) {
        ImGui::TextDisabled("Ask about the selected entity, renderer contracts, or engine architecture.");
    }
    for (const EditorAiChatMessage& message : messages) {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            message.role == "user"
                ? ImVec4(0.45f, 0.72f, 0.98f, 1.0f)
                : ImVec4(0.78f, 0.82f, 0.88f, 1.0f)
        );
        ImGui::TextUnformatted(message.role == "user" ? "You" : "Codex");
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", message.content.c_str());
        ImGui::Spacing();
    }
    if (running && !streamed_response.empty()) {
        ImGui::TextUnformatted("Codex");
        ImGui::TextWrapped("%s", streamed_response.c_str());
    }
    ImGui::EndChild();

    ImGui::InputTextMultiline(
        "##ai_prompt",
        m_prompt.data(),
        m_prompt.size(),
        ImVec2(-1.0f, 84.0f),
        ImGuiInputTextFlags_None
    );
    const bool can_send = provider_available && credentials_available && !running &&
        !pending_proposal.has_value() &&
        m_prompt[0] != '\0' && m_model[0] != '\0';
    ImGui::BeginDisabled(!can_send);
    if (ImGui::Button("Send to Codex")) start_request();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (running) {
        if (ImGui::Button("Cancel")) cancel_request();
    } else {
        ImGui::BeginDisabled(messages.empty() || pending_proposal.has_value());
        if (ImGui::Button("Clear")) {
            std::lock_guard lock(m_request_mutex);
            m_messages.clear();
            m_streamed_response.clear();
            m_request_error.clear();
            m_last_input_tokens = 0;
            m_last_output_tokens = 0;
            m_last_total_tokens = 0;
            m_last_tool_calls = 0;
            m_last_request_duration_ms = 0;
        }
        ImGui::EndDisabled();
    }
    if (!active_tool.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Tool: %s", active_tool.c_str());
    }
    if (!request_error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.32f, 1.0f), "%s", request_error.c_str());
    }
    if (!running && total_tokens > 0) {
        ImGui::TextDisabled(
            "Last request: %.2fs | %zu tools | %zu input | %zu output | %zu total tokens",
            static_cast<double>(request_duration_ms) / 1000.0,
            tool_calls,
            input_tokens,
            output_tokens,
            total_tokens
        );
    }
    ImGui::TextDisabled("Audit: Console + engine.log (request IDs; no prompt or credentials)");

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
        if (ImGui::Button("Copy JSON")) ImGui::SetClipboardText(context.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("Live scene metadata; document contents are loaded only on request");
        ImGui::BeginChild("AIContextPreview", ImVec2(0.0f, 180.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(context.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace Engine

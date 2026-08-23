#include "CodexAppServerProvider.hpp"

#include <filesystem>
#include <optional>

namespace Engine {
namespace {

constexpr int INITIALIZE_ID = 1;
constexpr int THREAD_OPEN_ID = 2;
constexpr int HISTORY_INJECT_ID = 3;
constexpr int TURN_START_ID = 4;
constexpr int THREAD_COMPACT_ID = 7;

void emit_event(
    const std::function<void(const EditorAiProviderEvent&)>& callback,
    EditorAiProviderEvent event
) {
    if (callback) callback(event);
}

std::string protocol_error(const nlohmann::json& message, std::string fallback) {
    if (!message.contains("error")) return fallback;
    const auto& error = message.at("error");
    if (error.is_object() && error.contains("message") && error.at("message").is_string()) {
        return error.at("message").get<std::string>();
    }
    if (error.is_string()) return error.get<std::string>();
    return fallback;
}

nlohmann::json dynamic_tools(const nlohmann::json& response_tools) {
    nlohmann::json tools = nlohmann::json::array();
    if (!response_tools.is_array()) return tools;
    for (const auto& tool : response_tools) {
        if (!tool.is_object() || tool.value("type", "") != "function" ||
            !tool.contains("name") || !tool.at("name").is_string()) {
            continue;
        }
        tools.push_back({
            {"type", "function"},
            {"name", tool.at("name")},
            {"description", tool.value("description", "Engine2D editor tool")},
            {"inputSchema", tool.value("parameters", nlohmann::json::object())}
        });
    }
    return tools;
}

nlohmann::json history_items(const std::vector<EditorAiChatMessage>& history) {
    nlohmann::json items = nlohmann::json::array();
    for (const EditorAiChatMessage& message : history) {
        if (message.content.empty()) continue;
        if (message.role == "user") {
            items.push_back({
                {"type", "message"},
                {"role", "user"},
                {"content", nlohmann::json::array({{
                    {"type", "input_text"}, {"text", message.content}
                }})}
            });
        } else if (message.role == "assistant") {
            items.push_back({
                {"type", "message"},
                {"role", "assistant"},
                {"content", nlohmann::json::array({{
                    {"type", "output_text"}, {"text", message.content}
                }})}
            });
        }
    }
    return items;
}

std::string completed_turn_text(const nlohmann::json& turn) {
    std::string text;
    if (!turn.is_object() || !turn.contains("items") || !turn.at("items").is_array()) return text;
    for (const auto& item : turn.at("items")) {
        if (item.is_object() && item.value("type", "") == "agentMessage" &&
            item.contains("text") && item.at("text").is_string()) {
            text += item.at("text").get<std::string>();
        }
    }
    return text;
}

std::string turn_error(const nlohmann::json& turn) {
    if (!turn.is_object() || !turn.contains("error") || turn.at("error").is_null()) {
        return "Codex turn failed";
    }
    const auto& error = turn.at("error");
    if (error.is_object() && error.contains("message") && error.at("message").is_string()) {
        return error.at("message").get<std::string>();
    }
    return "Codex turn failed";
}

class TransportStopGuard {
public:
    explicit TransportStopGuard(ICodexAppServerTransport* transport) : m_transport(transport) {}
    ~TransportStopGuard() { if (m_transport) m_transport->stop(); }
private:
    ICodexAppServerTransport* m_transport;
};

} // namespace

CodexAppServerProvider::CodexAppServerProvider(
    std::unique_ptr<ICodexAppServerTransport> transport
) : m_transport(std::move(transport)) {}

EditorAiProviderResult CodexAppServerProvider::run(
    const EditorAiProviderRequest& request,
    const std::function<void(const EditorAiProviderEvent&)>& on_event,
    const std::function<bool()>& is_cancelled
) {
    EditorAiProviderResult result;
    if (!m_transport) {
        result.error = "Codex App Server transport is unavailable";
        return result;
    }
    if (request.model.empty()) {
        result.error = "A Codex model is required";
        return result;
    }
    if (request.prompt.empty()) {
        result.error = "A prompt is required";
        return result;
    }
    const auto cancelled = [&is_cancelled]() { return is_cancelled && is_cancelled(); };
    if (cancelled()) {
        result.status = EditorAiProviderStatus::Cancelled;
        return result;
    }

    const std::string executable = request.codex_executable.empty()
        ? find_default_codex_executable()
        : request.codex_executable;
    std::string transport_error;
    if (!m_transport->start(executable, transport_error)) {
        result.error = transport_error.empty() ? "Could not start Codex App Server" : transport_error;
        return result;
    }
    TransportStopGuard stop_guard(m_transport.get());

    const auto send = [this, &result](const nlohmann::json& message) {
        std::string error;
        if (m_transport->write_line(message.dump(), error)) return true;
        result.error = error.empty() ? "Could not write to Codex App Server" : error;
        return false;
    };
    const auto read_message = [this, &result, &cancelled]() -> std::optional<nlohmann::json> {
        const CodexAppServerReadResult read = m_transport->read_line(cancelled);
        if (read.status == CodexAppServerReadStatus::Cancelled) {
            result.status = EditorAiProviderStatus::Cancelled;
            return std::nullopt;
        }
        if (read.status != CodexAppServerReadStatus::Line) {
            result.error = !read.error.empty() ? read.error : "Codex App Server closed unexpectedly";
            return std::nullopt;
        }
        nlohmann::json message = nlohmann::json::parse(read.line, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
            result.error = "Codex App Server returned malformed JSONL data";
            return std::nullopt;
        }
        return message;
    };
    const auto wait_for_response = [&](int id) -> std::optional<nlohmann::json> {
        while (true) {
            const auto message = read_message();
            if (!message) return std::nullopt;
            if (!message->contains("id") || message->at("id") != id) continue;
            if (message->contains("error")) {
                result.error = protocol_error(*message, "Codex App Server request failed");
                return std::nullopt;
            }
            if (!message->contains("result")) {
                result.error = "Codex App Server response did not contain a result";
                return std::nullopt;
            }
            return message->at("result");
        }
    };

    if (!send({
        {"id", INITIALIZE_ID},
        {"method", "initialize"},
        {"params", {
            {"clientInfo", {{"name", "engine2d-editor"}, {"title", "Engine2D Editor"}, {"version", "0.1.0"}}},
            {"capabilities", {{"experimentalApi", true}}}
        }}
    }) || !wait_for_response(INITIALIZE_ID)) return result;
    if (!send({{"method", "initialized"}})) return result;

    std::error_code path_error;
    std::filesystem::path project_root = std::filesystem::absolute(request.project_root, path_error);
    if (path_error) project_root = request.project_root;
    const std::string root = project_root.lexically_normal().string();
    std::string instructions =
        "You are embedded in Engine2D Editor. Use dynamic editor tools as the primary source for live "
        "scene context and authoring. Full Access is enabled: you may inspect, create, modify, and run "
        "files and commands in the local environment. Prefer the project workspace and explain material "
        "filesystem or command effects in your final response. Use validated editor tools for scene "
        "changes whenever Undo/Redo and serialization guarantees are required.";
    if (!request.instructions.empty()) instructions += "\n\n" + request.instructions;
    nlohmann::json thread_params{
        {"model", request.model},
        {"cwd", root},
        {"runtimeWorkspaceRoots", nlohmann::json::array({root})},
        {"developerInstructions", instructions},
        {"approvalPolicy", "never"},
        {"sandbox", "danger-full-access"}
    };
    const bool resuming_thread = !request.provider_thread_id.empty();
    if (resuming_thread) {
        thread_params["threadId"] = request.provider_thread_id;
    } else {
        thread_params["ephemeral"] = !request.persist_thread;
        thread_params["dynamicTools"] = dynamic_tools(request.tools);
        thread_params["serviceName"] = "engine2d_editor";
    }
    if (!send({
        {"id", THREAD_OPEN_ID},
        {"method", resuming_thread ? "thread/resume" : "thread/start"},
        {"params", std::move(thread_params)}
    })) return result;
    const auto thread_open = wait_for_response(THREAD_OPEN_ID);
    if (!thread_open) return result;
    if (!thread_open->contains("thread") || !thread_open->at("thread").is_object()) {
        result.error = "Codex App Server did not return a thread";
        return result;
    }
    const auto& thread = thread_open->at("thread");
    const std::string thread_id = thread.value("id", "");
    if (thread_id.empty()) {
        result.error = "Codex App Server returned an invalid thread id";
        return result;
    }
    result.provider_thread_id = thread_id;
    result.provider_session_id = thread.value("sessionId", thread_id);

    const nlohmann::json prior_items = resuming_thread
        ? nlohmann::json::array()
        : history_items(request.history);
    if (!prior_items.empty()) {
        if (!send({
            {"id", HISTORY_INJECT_ID},
            {"method", "thread/inject_items"},
            {"params", {{"threadId", thread_id}, {"items", prior_items}}}
        }) || !wait_for_response(HISTORY_INJECT_ID)) return result;
    }

    if (request.compact_thread_before_turn && resuming_thread) {
        if (!send({
            {"id", THREAD_COMPACT_ID},
            {"method", "thread/compact/start"},
            {"params", {{"threadId", thread_id}}}
        })) return result;

        bool request_acknowledged = false;
        bool turn_completed = false;
        std::string compaction_turn_id;
        while (!request_acknowledged || !turn_completed) {
            const auto message = read_message();
            if (!message) {
                if (result.status == EditorAiProviderStatus::Cancelled &&
                    !compaction_turn_id.empty()) {
                    send({
                        {"id", 8},
                        {"method", "turn/interrupt"},
                        {"params", {
                            {"threadId", thread_id},
                            {"turnId", compaction_turn_id}
                        }}
                    });
                }
                return result;
            }
            if (message->contains("id") && message->at("id") == THREAD_COMPACT_ID) {
                if (message->contains("error")) {
                    result.error = protocol_error(
                        *message, "Codex could not start context compaction"
                    );
                    return result;
                }
                if (!message->contains("result")) {
                    result.error = "Codex context compaction response did not contain a result";
                    return result;
                }
                request_acknowledged = true;
                continue;
            }

            const std::string method = message->value("method", "");
            const auto& params = message->value("params", nlohmann::json::object());
            if (params.value("threadId", "") != thread_id) continue;
            if (method == "turn/started") {
                compaction_turn_id = params.value(
                    "turn", nlohmann::json::object()
                ).value("id", "");
            } else if (method == "item/started") {
                const auto& item = params.value("item", nlohmann::json::object());
                if (item.value("type", "") != "contextCompaction") continue;
                if (compaction_turn_id.empty()) {
                    compaction_turn_id = params.value("turnId", "");
                }
                EditorAiProviderEvent event;
                event.type = EditorAiProviderEventType::ItemStarted;
                event.item_id = item.value("id", "");
                event.item_type = "contextCompaction";
                event.item_status = "inProgress";
                emit_event(on_event, std::move(event));
            } else if (method == "item/completed") {
                const auto& item = params.value("item", nlohmann::json::object());
                if (item.value("type", "") != "contextCompaction") continue;
                EditorAiProviderEvent event;
                event.type = EditorAiProviderEventType::ItemCompleted;
                event.item_id = item.value("id", "");
                event.item_type = "contextCompaction";
                event.item_status = "completed";
                emit_event(on_event, std::move(event));
            } else if (method == "turn/completed") {
                const auto& turn = params.value("turn", nlohmann::json::object());
                const std::string completed_turn_id = turn.value("id", "");
                if (!compaction_turn_id.empty() &&
                    completed_turn_id != compaction_turn_id) {
                    continue;
                }
                compaction_turn_id = completed_turn_id;
                const std::string status = turn.value("status", "failed");
                if (status != "completed") {
                    result.status = status == "interrupted"
                        ? EditorAiProviderStatus::Cancelled
                        : EditorAiProviderStatus::Failed;
                    result.error = status == "interrupted"
                        ? "Codex context compaction was interrupted"
                        : turn_error(turn);
                    return result;
                }
                turn_completed = true;
            }
        }
        result.context_compacted = true;
    }

    nlohmann::json turn_input = nlohmann::json::array();
    for (const EditorAiSkillReference& skill : request.skills) {
        if (skill.name.empty() || skill.path.empty()) continue;
        turn_input.push_back({
            {"type", "skill"},
            {"name", skill.name},
            {"path", skill.path}
        });
    }
    turn_input.push_back({
        {"type", "text"},
        {"text", request.prompt},
        {"text_elements", nlohmann::json::array()}
    });
    if (!send({
        {"id", TURN_START_ID},
        {"method", "turn/start"},
        {"params", {
            {"threadId", thread_id},
            {"input", std::move(turn_input)},
            {"effort", request.reasoning_effort.empty() ? "low" : request.reasoning_effort},
            {"summary", request.reasoning_summary.empty() ? "auto" : request.reasoning_summary}
        }}
    })) return result;
    emit_event(on_event, {EditorAiProviderEventType::Started, {}, {}});

    std::string turn_id;
    std::size_t tool_failures = 0;
    while (true) {
        const auto message = read_message();
        if (!message) {
            if (result.status == EditorAiProviderStatus::Cancelled && !turn_id.empty()) {
                send({
                    {"id", 5},
                    {"method", "turn/interrupt"},
                    {"params", {{"threadId", thread_id}, {"turnId", turn_id}}}
                });
            }
            return result;
        }

        if (message->contains("id") && message->at("id") == TURN_START_ID) {
            if (message->contains("error")) {
                result.error = protocol_error(*message, "Codex could not start the turn");
                return result;
            }
            if (message->contains("result") && message->at("result").contains("turn")) {
                turn_id = message->at("result").at("turn").value("id", "");
                result.response_id = turn_id;
            }
            continue;
        }

        const std::string method = message->value("method", "");
        if (method == "item/agentMessage/delta") {
            const std::string delta = message->value("params", nlohmann::json::object()).value("delta", "");
            result.text += delta;
            emit_event(on_event, {EditorAiProviderEventType::TextDelta, delta, {}});
        } else if (method == "item/reasoning/summaryTextDelta") {
            const auto& params = message->value("params", nlohmann::json::object());
            EditorAiProviderEvent event;
            event.type = EditorAiProviderEventType::ReasoningSummaryDelta;
            event.text = params.value("delta", "");
            event.item_id = params.value("itemId", "");
            emit_event(on_event, std::move(event));
        } else if (method == "item/reasoning/summaryPartAdded") {
            const auto& params = message->value("params", nlohmann::json::object());
            if (params.value("summaryIndex", std::size_t{0}) > 0) {
                EditorAiProviderEvent event;
                event.type = EditorAiProviderEventType::ReasoningSummaryDelta;
                event.text = "\n\n";
                event.item_id = params.value("itemId", "");
                emit_event(on_event, std::move(event));
            }
        } else if (method == "thread/tokenUsage/updated") {
            const auto& params = message->value("params", nlohmann::json::object());
            if (params.contains("tokenUsage") && params.at("tokenUsage").contains("last")) {
                const auto& usage = params.at("tokenUsage").at("last");
                result.input_tokens = usage.value("inputTokens", std::size_t{0});
                result.output_tokens = usage.value("outputTokens", std::size_t{0});
                result.total_tokens = usage.value("totalTokens", std::size_t{0});
            }
        } else if (method == "item/started") {
            const auto& params = message->value("params", nlohmann::json::object());
            const auto& item = params.value("item", nlohmann::json::object());
            EditorAiProviderEvent activity;
            activity.type = EditorAiProviderEventType::ItemStarted;
            activity.item_id = item.value("id", "");
            activity.item_type = item.value("type", "unknown");
            activity.item_status = item.value("status", "inProgress");
            emit_event(on_event, std::move(activity));
            if (item.value("type", "") == "imageGeneration") {
                emit_event(on_event, {
                    EditorAiProviderEventType::ToolStarted, {}, "Codex ImageGen"
                });
            }
        } else if (method == "item/completed") {
            const auto& params = message->value("params", nlohmann::json::object());
            const auto& item = params.value("item", nlohmann::json::object());
            EditorAiProviderEvent activity;
            activity.type = EditorAiProviderEventType::ItemCompleted;
            activity.item_id = item.value("id", "");
            activity.item_type = item.value("type", "unknown");
            activity.item_status = item.value("status", "completed");
            emit_event(on_event, std::move(activity));
            const bool image_generation_item = item.value("type", "") == "imageGeneration";
            if (image_generation_item &&
                item.value("status", "") == "completed" &&
                item.contains("savedPath") && item.at("savedPath").is_string()) {
                EditorAiProviderEvent event;
                event.type = EditorAiProviderEventType::ImageGenerated;
                event.artifact_id = item.value("id", "");
                event.artifact_path = item.at("savedPath").get<std::string>();
                event.transparent_background = item.value("transparentBackground", false);
                emit_event(on_event, std::move(event));
            }
            if (image_generation_item) emit_event(on_event, {
                EditorAiProviderEventType::ToolFinished,
                item.value("status", "") == "completed" ? "success" : "failed",
                "Codex ImageGen"
            });
        } else if (method == "item/tool/call" && message->contains("id")) {
            const auto& params = message->value("params", nlohmann::json::object());
            const std::string name = params.value("tool", "");
            emit_event(on_event, {EditorAiProviderEventType::ToolStarted, {}, name});
            EditorAiToolResult tool_result;
            if (++result.tool_call_count > request.max_tool_rounds) {
                tool_result = {false, {}, "Codex exceeded the editor tool-call limit"};
            } else if (name.empty()) {
                tool_result = {false, {}, "Codex requested an unnamed editor tool"};
            } else if (!request.execute_tool) {
                tool_result = {false, {}, "No editor tool executor is configured"};
            } else {
                tool_result = request.execute_tool(name, params.value("arguments", nlohmann::json::object()));
            }
            nlohmann::json output{{"success", tool_result.success}};
            if (tool_result.success) output["result"] = tool_result.output;
            else output["error"] = tool_result.error;
            nlohmann::json content_items = nlohmann::json::array({
                {{"type", "inputText"}, {"text", output.dump()}}
            });
            if (tool_result.success && !tool_result.image_url.empty()) {
                content_items.push_back({
                    {"type", "inputImage"},
                    {"imageUrl", tool_result.image_url}
                });
            }
            if (!send({
                {"id", message->at("id")},
                {"result", {
                    {"contentItems", std::move(content_items)},
                    {"success", tool_result.success}
                }}
            })) return result;
            emit_event(on_event, {
                EditorAiProviderEventType::ToolFinished,
                tool_result.success ? "success" : tool_result.error,
                name
            });
            const bool call_limit_reached = result.tool_call_count > request.max_tool_rounds;
            const bool failure_limit_reached = !tool_result.success &&
                ++tool_failures >= request.max_tool_failures;
            if (call_limit_reached || failure_limit_reached) {
                if (!turn_id.empty()) {
                    send({
                        {"id", 6},
                        {"method", "turn/interrupt"},
                        {"params", {{"threadId", thread_id}, {"turnId", turn_id}}}
                    });
                }
                result.error = call_limit_reached
                    ? "Codex exceeded the editor tool-call limit"
                    : "Codex produced too many invalid editor tool calls";
                return result;
            }
        } else if (method == "turn/completed") {
            const auto& turn = message->value("params", nlohmann::json::object()).value(
                "turn", nlohmann::json::object()
            );
            if (result.text.empty()) {
                const std::string final_text = completed_turn_text(turn);
                if (!final_text.empty()) {
                    result.text = final_text;
                    emit_event(on_event, {EditorAiProviderEventType::TextDelta, final_text, {}});
                }
            }
            const std::string status = turn.value("status", "failed");
            if (status == "completed") {
                result.status = EditorAiProviderStatus::Completed;
            } else if (status == "interrupted") {
                result.status = EditorAiProviderStatus::Cancelled;
            } else {
                result.error = turn_error(turn);
            }
            return result;
        } else if (message->contains("id") && !method.empty()) {
            send({
                {"id", message->at("id")},
                {"error", {{"code", -32601}, {"message", "Unsupported App Server request: " + method}}}
            });
        }
    }
}

} // namespace Engine

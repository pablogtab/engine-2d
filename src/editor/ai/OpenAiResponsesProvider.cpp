#include "OpenAiResponsesProvider.hpp"

#include <algorithm>
#include <optional>

namespace Engine {
namespace {

constexpr const char* OPENAI_RESPONSES_URL = "https://api.openai.com/v1/responses";

class SseDecoder {
public:
    using EventCallback = std::function<bool(const std::string&)>;

    explicit SseDecoder(EventCallback callback) : m_callback(std::move(callback)) {}

    bool feed(std::string_view chunk) {
        m_buffer.append(chunk);
        std::size_t newline = 0;
        while ((newline = m_buffer.find('\n')) != std::string::npos) {
            std::string line = m_buffer.substr(0, newline);
            m_buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!consume_line(line)) return false;
        }
        return true;
    }

    bool finish() {
        if (!m_buffer.empty()) {
            std::string line = std::move(m_buffer);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!consume_line(line)) return false;
        }
        return dispatch();
    }

private:
    bool consume_line(const std::string& line) {
        if (line.empty()) return dispatch();
        if (!line.starts_with("data:")) return true;
        std::string data = line.substr(5);
        if (!data.empty() && data.front() == ' ') data.erase(data.begin());
        if (!m_data.empty()) m_data.push_back('\n');
        m_data += data;
        return true;
    }

    bool dispatch() {
        if (m_data.empty()) return true;
        std::string data = std::move(m_data);
        m_data.clear();
        if (data == "[DONE]") return true;
        return m_callback(data);
    }

    EventCallback m_callback;
    std::string m_buffer;
    std::string m_data;
};

std::string response_error_message(const std::string& body, long status_code) {
    if (!body.empty()) {
        const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("error")) {
            const auto& error = parsed.at("error");
            if (error.is_object() && error.contains("message") && error.at("message").is_string()) {
                return error.at("message").get<std::string>();
            }
        }
    }
    if (status_code == 401) return "OpenAI rejected the API key";
    if (status_code == 429) return "OpenAI rate limit reached";
    return "OpenAI request failed with HTTP " + std::to_string(status_code);
}

std::string extract_output_text(const nlohmann::json& response) {
    std::string result;
    if (!response.contains("output") || !response.at("output").is_array()) return result;
    for (const auto& item : response.at("output")) {
        if (!item.is_object() || item.value("type", "") != "message") continue;
        if (!item.contains("content") || !item.at("content").is_array()) continue;
        for (const auto& content : item.at("content")) {
            if (content.is_object() && content.value("type", "") == "output_text" &&
                content.contains("text") && content.at("text").is_string()) {
                result += content.at("text").get<std::string>();
            }
        }
    }
    return result;
}

nlohmann::json make_request_body(
    const OpenAiResponsesRequest& request,
    const nlohmann::json& input
) {
    return {
        {"model", request.model},
        {"instructions", request.instructions.empty()
            ? "You are the read-only assistant inside Engine2D Editor. Use the provided tools whenever "
              "the answer depends on the active scene or selection. Explain findings clearly and never "
              "claim that you modified editor state."
            : request.instructions},
        {"input", input},
        {"tools", request.tools},
        {"tool_choice", "auto"},
        {"parallel_tool_calls", false},
        {"reasoning", {{"effort", request.reasoning_effort.empty() ? "low" : request.reasoning_effort}}},
        {"stream", true},
        {"store", false}
    };
}

void emit_event(
    const std::function<void(const OpenAiProviderEvent&)>& callback,
    OpenAiProviderEvent event
) {
    if (callback) callback(event);
}

} // namespace

OpenAiResponsesProvider::OpenAiResponsesProvider(std::unique_ptr<IEditorAiHttpTransport> transport)
    : m_transport(std::move(transport)) {}

OpenAiResponsesResult OpenAiResponsesProvider::run(
    const OpenAiResponsesRequest& request,
    const std::function<void(const OpenAiProviderEvent&)>& on_event,
    const std::function<bool()>& is_cancelled
) {
    OpenAiResponsesResult result;
    if (!m_transport) {
        result.error = "No HTTP transport is available in this editor build";
        return result;
    }
    if (request.api_key.empty()) {
        result.error = "OPENAI_API_KEY is not configured";
        return result;
    }
    if (request.model.empty()) {
        result.error = "An OpenAI model is required";
        return result;
    }
    if (request.prompt.empty()) {
        result.error = "A prompt is required";
        return result;
    }
    const auto cancelled = [&is_cancelled]() { return is_cancelled && is_cancelled(); };
    if (cancelled()) {
        result.status = OpenAiProviderStatus::Cancelled;
        return result;
    }

    nlohmann::json input = nlohmann::json::array();
    for (const EditorAiChatMessage& message : request.history) {
        if ((message.role == "user" || message.role == "assistant") && !message.content.empty()) {
            input.push_back({{"role", message.role}, {"content", message.content}});
        }
    }
    if (request.images.empty()) {
        input.push_back({{"role", "user"}, {"content", request.prompt}});
    } else {
        nlohmann::json content = nlohmann::json::array({{
            {"type", "input_text"}, {"text", request.prompt}
        }});
        for (const EditorAiImageAttachment& image : request.images) {
            if (image.data_url.empty()) continue;
            content.push_back({
                {"type", "input_image"},
                {"image_url", image.data_url},
                {"detail", image.detail.empty() ? "auto" : image.detail}
            });
        }
        input.push_back({{"role", "user"}, {"content", std::move(content)}});
    }
    emit_event(on_event, {OpenAiProviderEventType::Started, {}, {}});

    std::size_t tool_failures = 0;
    for (std::size_t round = 0; round <= request.max_tool_rounds; ++round) {
        if (cancelled()) {
            result.status = OpenAiProviderStatus::Cancelled;
            return result;
        }

        nlohmann::json completed_response;
        std::string stream_error;
        std::string round_text;
        SseDecoder decoder([&](const std::string& data) {
            const nlohmann::json event = nlohmann::json::parse(data, nullptr, false);
            if (event.is_discarded() || !event.is_object()) {
                stream_error = "OpenAI returned malformed streaming data";
                return false;
            }
            const std::string type = event.value("type", "");
            if (type == "response.output_text.delta") {
                const std::string delta = event.value("delta", "");
                round_text += delta;
                result.text += delta;
                emit_event(on_event, {OpenAiProviderEventType::TextDelta, delta, {}});
            } else if (type == "response.completed") {
                if (event.contains("response") && event.at("response").is_object()) {
                    completed_response = event.at("response");
                }
            } else if (type == "response.failed") {
                stream_error = "OpenAI response failed";
                if (event.contains("response") && event.at("response").contains("error")) {
                    const auto& error = event.at("response").at("error");
                    if (error.is_object() && error.contains("message") && error.at("message").is_string()) {
                        stream_error = error.at("message").get<std::string>();
                    }
                }
                return false;
            } else if (type == "error") {
                stream_error = event.value("message", "OpenAI streaming error");
                if (event.contains("error") && event.at("error").is_object() &&
                    event.at("error").contains("message") && event.at("error").at("message").is_string()) {
                    stream_error = event.at("error").at("message").get<std::string>();
                }
                return false;
            }
            return !cancelled();
        });

        const EditorAiHttpRequest http_request{
            OPENAI_RESPONSES_URL,
            {
                "Authorization: Bearer " + request.api_key,
                "Content-Type: application/json",
                "Accept: text/event-stream"
            },
            make_request_body(request, input).dump()
        };
        const EditorAiHttpResponse http_response = m_transport->post(
            http_request,
            [&decoder](std::string_view chunk) { return decoder.feed(chunk); },
            cancelled
        );
        result.http_status = http_response.status_code;

        if (http_response.cancelled || cancelled()) {
            result.status = OpenAiProviderStatus::Cancelled;
            return result;
        }
        if (!http_response.error.empty()) {
            result.error = http_response.error;
            return result;
        }
        if (http_response.status_code < 200 || http_response.status_code >= 300) {
            result.error = response_error_message(http_response.body, http_response.status_code);
            return result;
        }
        if (!decoder.finish() || !stream_error.empty()) {
            result.error = stream_error.empty() ? "OpenAI streaming response ended unexpectedly" : stream_error;
            return result;
        }
        if (completed_response.empty()) {
            result.error = "OpenAI stream did not include response.completed";
            return result;
        }

        result.response_id = completed_response.value("id", "");
        if (completed_response.contains("usage") && completed_response.at("usage").is_object()) {
            const auto& usage = completed_response.at("usage");
            result.input_tokens += usage.value("input_tokens", std::size_t{0});
            result.output_tokens += usage.value("output_tokens", std::size_t{0});
            result.total_tokens += usage.value("total_tokens", std::size_t{0});
        }
        if (round_text.empty()) {
            const std::string completed_text = extract_output_text(completed_response);
            if (!completed_text.empty()) {
                result.text += completed_text;
                emit_event(on_event, {OpenAiProviderEventType::TextDelta, completed_text, {}});
            }
        }

        std::vector<nlohmann::json> function_calls;
        if (completed_response.contains("output") && completed_response.at("output").is_array()) {
            for (const auto& item : completed_response.at("output")) {
                if (item.is_object() && item.value("type", "") == "function_call") {
                    function_calls.push_back(item);
                }
            }
        }
        if (function_calls.empty()) {
            result.status = OpenAiProviderStatus::Completed;
            return result;
        }
        if (round == request.max_tool_rounds) {
            result.error = "OpenAI exceeded the editor tool-call round limit";
            return result;
        }

        for (const auto& item : completed_response.at("output")) input.push_back(item);
        for (const auto& call : function_calls) {
            const std::string name = call.value("name", "");
            const std::string call_id = call.value("call_id", "");
            const std::string arguments_text = call.value("arguments", "{}");
            if (name.empty() || call_id.empty()) {
                result.error = "OpenAI returned an invalid function call";
                return result;
            }

            emit_event(on_event, {OpenAiProviderEventType::ToolStarted, {}, name});
            const nlohmann::json arguments = nlohmann::json::parse(arguments_text, nullptr, false);
            EditorAiToolResult tool_result;
            if (arguments.is_discarded()) {
                tool_result = {false, {}, "Tool arguments were not valid JSON"};
            } else if (!request.execute_tool) {
                tool_result = {false, {}, "No editor tool executor is configured"};
            } else {
                tool_result = request.execute_tool(name, arguments);
            }

            nlohmann::json tool_output{
                {"success", tool_result.success}
            };
            if (tool_result.success) {
                tool_output["result"] = tool_result.output;
            } else {
                tool_output["error"] = tool_result.error;
            }
            nlohmann::json output_content = tool_output.dump();
            if (tool_result.success && !tool_result.image_url.empty()) {
                output_content = nlohmann::json::array({
                    {{"type", "input_text"}, {"text", tool_output.dump()}},
                    {
                        {"type", "input_image"},
                        {"image_url", tool_result.image_url},
                        {"detail", "high"}
                    }
                });
            }
            input.push_back({
                {"type", "function_call_output"},
                {"call_id", call_id},
                {"output", std::move(output_content)}
            });
            ++result.tool_call_count;
            emit_event(on_event, {
                OpenAiProviderEventType::ToolFinished,
                tool_result.success ? "success" : tool_result.error,
                name
            });
            if (!tool_result.success && ++tool_failures >= request.max_tool_failures) {
                result.error = "OpenAI produced too many invalid editor tool calls";
                return result;
            }
        }
    }

    result.error = "OpenAI tool-call loop ended unexpectedly";
    return result;
}

} // namespace Engine

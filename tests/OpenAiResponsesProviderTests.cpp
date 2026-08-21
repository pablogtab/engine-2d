#include "editor/ai/OpenAiResponsesProvider.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace Engine;

struct ScriptedHttpResponse {
    long status_code{200};
    std::vector<std::string> chunks;
    std::string body;
    std::string error;
    bool cancelled{false};
};

struct FakeTransportState {
    std::vector<ScriptedHttpResponse> scripts;
    std::vector<EditorAiHttpRequest> requests;
};

class FakeTransport final : public IEditorAiHttpTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeTransportState> state) : m_state(std::move(state)) {}

    EditorAiHttpResponse post(
        const EditorAiHttpRequest& request,
        const std::function<bool(std::string_view)>& on_chunk,
        const std::function<bool()>& is_cancelled
    ) override {
        m_state->requests.push_back(request);
        assert(m_state->requests.size() <= m_state->scripts.size());
        const ScriptedHttpResponse& script = m_state->scripts.at(m_state->requests.size() - 1);
        for (const std::string& chunk : script.chunks) {
            if ((is_cancelled && is_cancelled()) || !on_chunk(chunk)) break;
        }
        return {
            script.status_code,
            script.body,
            script.error,
            script.cancelled || (is_cancelled && is_cancelled())
        };
    }

private:
    std::shared_ptr<FakeTransportState> m_state;
};

std::string completed_event(const nlohmann::json& output, const std::string& id = "resp_test") {
    return "data: " + nlohmann::json({
        {"type", "response.completed"},
        {"response", {
            {"id", id},
            {"output", output},
            {"usage", {{"input_tokens", 10}, {"output_tokens", 4}, {"total_tokens", 14}}}
        }}
    }).dump() + "\n\n";
}

OpenAiResponsesRequest basic_request() {
    OpenAiResponsesRequest request;
    request.api_key = "test-key";
    request.model = "gpt-5.6-sol";
    request.prompt = "Describe the current scene.";
    request.tools = nlohmann::json::array({
        {
            {"type", "function"},
            {"name", "get_scene_summary"},
            {"description", "Read the scene."},
            {"parameters", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"required", nlohmann::json::array()},
                {"additionalProperties", false}
            }},
            {"strict", true}
        }
    });
    return request;
}

void test_streaming_request_and_chunk_boundaries() {
    auto state = std::make_shared<FakeTransportState>();
    state->scripts.push_back({
        200,
        {
            "data: {\"type\":\"response.output_text.delta\",\"del",
            "ta\":\"Hello \"}\n\ndata: {\"type\":\"response.output_text.delta\",\"delta\":\"engine\"}\n\n",
            completed_event(nlohmann::json::array({
                {
                    {"type", "message"},
                    {"content", nlohmann::json::array({{{"type", "output_text"}, {"text", "Hello engine"}}})}
                }
            }))
        }
    });
    OpenAiResponsesProvider provider(std::make_unique<FakeTransport>(state));
    std::string streamed;
    const OpenAiResponsesResult result = provider.run(
        basic_request(),
        [&streamed](const OpenAiProviderEvent& event) {
            if (event.type == OpenAiProviderEventType::TextDelta) streamed += event.text;
        }
    );

    assert(result.status == OpenAiProviderStatus::Completed);
    assert(result.text == "Hello engine");
    assert(streamed == "Hello engine");
    assert(result.response_id == "resp_test");
    assert(result.input_tokens == 10);
    assert(result.output_tokens == 4);
    assert(result.total_tokens == 14);
    assert(state->requests.size() == 1);
    const nlohmann::json body = nlohmann::json::parse(state->requests.front().body);
    assert(body.at("model") == "gpt-5.6-sol");
    assert(body.at("stream") == true);
    assert(body.at("store") == false);
    assert(body.at("reasoning").at("effort") == "low");
    assert(body.at("tools").size() == 1);
    assert(body.at("input").back().at("role") == "user");
}

void test_invalid_tool_call_budget_stops_retries() {
    auto state = std::make_shared<FakeTransportState>();
    for (int index = 0; index < 2; ++index) {
        state->scripts.push_back({200, {completed_event(nlohmann::json::array({{
            {"type", "function_call"},
            {"call_id", "bad_" + std::to_string(index)},
            {"name", "get_scene_summary"},
            {"arguments", "{}"}
        }}))}});
    }
    OpenAiResponsesProvider provider(std::make_unique<FakeTransport>(state));
    OpenAiResponsesRequest request = basic_request();
    request.max_tool_failures = 2;
    request.execute_tool = [](const std::string&, const nlohmann::json&) {
        return EditorAiToolResult{false, {}, "scripted invalid call"};
    };

    const OpenAiResponsesResult result = provider.run(request, {});
    assert(result.status == OpenAiProviderStatus::Failed);
    assert(result.error == "OpenAI produced too many invalid editor tool calls");
    assert(result.tool_call_count == 2);
    assert(state->requests.size() == 2);
}

void test_function_call_output_loop() {
    auto state = std::make_shared<FakeTransportState>();
    const nlohmann::json function_call{
        {"type", "function_call"},
        {"call_id", "call_scene"},
        {"name", "get_scene_summary"},
        {"arguments", "{}"}
    };
    state->scripts.push_back({200, {completed_event(nlohmann::json::array({function_call}), "resp_tools")}});
    state->scripts.push_back({
        200,
        {
            "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Two entities.\"}\n\n",
            completed_event(nlohmann::json::array(), "resp_final")
        }
    });
    OpenAiResponsesProvider provider(std::make_unique<FakeTransport>(state));
    OpenAiResponsesRequest request = basic_request();
    std::size_t executions = 0;
    request.execute_tool = [&executions](const std::string& name, const nlohmann::json& arguments) {
        ++executions;
        assert(name == "get_scene_summary");
        assert(arguments == nlohmann::json::object());
        return EditorAiToolResult{true, {{"entity_count", 2}}, {}};
    };

    const OpenAiResponsesResult result = provider.run(request, {});
    assert(result.status == OpenAiProviderStatus::Completed);
    assert(result.text == "Two entities.");
    assert(result.tool_call_count == 1);
    assert(executions == 1);
    assert(state->requests.size() == 2);

    const nlohmann::json second_body = nlohmann::json::parse(state->requests.at(1).body);
    const auto& input = second_body.at("input");
    assert(input.size() == 3);
    assert(input.at(1).at("type") == "function_call");
    assert(input.at(2).at("type") == "function_call_output");
    assert(input.at(2).at("call_id") == "call_scene");
    const nlohmann::json output = nlohmann::json::parse(input.at(2).at("output").get<std::string>());
    assert(output.at("success") == true);
    assert(output.at("result").at("entity_count") == 2);
}

void test_authentication_and_rate_limit_errors_are_mapped() {
    auto auth_state = std::make_shared<FakeTransportState>();
    auth_state->scripts.push_back({401, {}, R"({"error":{"message":"Invalid project key"}})"});
    OpenAiResponsesProvider auth_provider(std::make_unique<FakeTransport>(auth_state));
    const OpenAiResponsesResult auth = auth_provider.run(basic_request(), {});
    assert(auth.status == OpenAiProviderStatus::Failed);
    assert(auth.http_status == 401);
    assert(auth.error == "Invalid project key");

    auto rate_state = std::make_shared<FakeTransportState>();
    rate_state->scripts.push_back({429, {}, {}});
    OpenAiResponsesProvider rate_provider(std::make_unique<FakeTransport>(rate_state));
    const OpenAiResponsesResult rate = rate_provider.run(basic_request(), {});
    assert(rate.status == OpenAiProviderStatus::Failed);
    assert(rate.error == "OpenAI rate limit reached");
}

void test_stream_errors_and_cancellation() {
    auto malformed_state = std::make_shared<FakeTransportState>();
    malformed_state->scripts.push_back({200, {"data: not-json\n\n"}});
    OpenAiResponsesProvider malformed_provider(std::make_unique<FakeTransport>(malformed_state));
    const OpenAiResponsesResult malformed = malformed_provider.run(basic_request(), {});
    assert(malformed.status == OpenAiProviderStatus::Failed);
    assert(malformed.error == "OpenAI returned malformed streaming data");

    auto cancelled_state = std::make_shared<FakeTransportState>();
    OpenAiResponsesProvider cancelled_provider(std::make_unique<FakeTransport>(cancelled_state));
    const OpenAiResponsesResult cancelled = cancelled_provider.run(
        basic_request(), {}, []() { return true; }
    );
    assert(cancelled.status == OpenAiProviderStatus::Cancelled);
    assert(cancelled_state->requests.empty());
}

} // namespace

int main() {
    test_streaming_request_and_chunk_boundaries();
    test_function_call_output_loop();
    test_invalid_tool_call_budget_stops_retries();
    test_authentication_and_rate_limit_errors_are_mapped();
    test_stream_errors_and_cancellation();
    std::cout << "OpenAI Responses provider tests passed\n";
    return 0;
}

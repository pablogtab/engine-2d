#include "editor/ai/CodexAppServerProvider.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace Engine;

struct FakeTransportState {
    bool start_succeeds{true};
    std::string started_executable;
    std::vector<std::string> reads;
    std::vector<nlohmann::json> writes;
    std::size_t read_index{0};
    bool stopped{false};
};

class FakeTransport final : public ICodexAppServerTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeTransportState> state) : m_state(std::move(state)) {}

    bool start(const std::string& executable, std::string& error) override {
        m_state->started_executable = executable;
        if (m_state->start_succeeds) return true;
        error = "scripted start failure";
        return false;
    }

    bool write_line(const std::string& line, std::string&) override {
        m_state->writes.push_back(nlohmann::json::parse(line));
        return true;
    }

    CodexAppServerReadResult read_line(const std::function<bool()>& is_cancelled) override {
        if (is_cancelled && is_cancelled()) {
            return {CodexAppServerReadStatus::Cancelled, {}, {}};
        }
        if (m_state->read_index >= m_state->reads.size()) {
            return {CodexAppServerReadStatus::Closed, {}, "script exhausted"};
        }
        return {
            CodexAppServerReadStatus::Line,
            m_state->reads.at(m_state->read_index++),
            {}
        };
    }

    void stop() override { m_state->stopped = true; }

private:
    std::shared_ptr<FakeTransportState> m_state;
};

std::string json_line(nlohmann::json value) {
    return value.dump();
}

EditorAiProviderRequest basic_request() {
    EditorAiProviderRequest request;
    request.model = "gpt-5.6-sol";
    request.prompt = "Describe the renderer contract.";
    request.project_root = "/tmp/engine2d-project";
    request.codex_executable = "/test/codex";
    request.tools = nlohmann::json::array({
        {
            {"type", "function"},
            {"name", "get_scene_summary"},
            {"description", "Read the current scene."},
            {"parameters", {
                {"type", "object"},
                {"properties", nlohmann::json::object()},
                {"additionalProperties", false}
            }},
            {"strict", true}
        }
    });
    return request;
}

std::shared_ptr<FakeTransportState> standard_script() {
    auto state = std::make_shared<FakeTransportState>();
    state->reads = {
        json_line({{"id", 1}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 2}, {"result", {{"thread", {{"id", "thread-test"}}}}}}),
        json_line({{"id", 4}, {"result", {{"turn", {{"id", "turn-test"}, {"status", "inProgress"}, {"items", nlohmann::json::array()}}}}}}),
        json_line({{"method", "item/agentMessage/delta"}, {"params", {{"threadId", "thread-test"}, {"turnId", "turn-test"}, {"itemId", "message-test"}, {"delta", "Renderer ready."}}}}),
        json_line({{"method", "thread/tokenUsage/updated"}, {"params", {
            {"threadId", "thread-test"},
            {"turnId", "turn-test"},
            {"tokenUsage", {{"last", {{"inputTokens", 42}, {"outputTokens", 7}, {"totalTokens", 49}}}}}
        }}}),
        json_line({{"method", "turn/completed"}, {"params", {
            {"threadId", "thread-test"},
            {"turn", {{"id", "turn-test"}, {"status", "completed"}, {"items", nlohmann::json::array()}}}
        }}})
    };
    return state;
}

void test_subscription_protocol_streaming_and_read_only_sandbox() {
    auto state = standard_script();
    CodexAppServerProvider provider(std::make_unique<FakeTransport>(state));
    EditorAiProviderRequest request = basic_request();
    std::string streamed;
    const EditorAiProviderResult result = provider.run(
        request,
        [&streamed](const EditorAiProviderEvent& event) {
            if (event.type == EditorAiProviderEventType::TextDelta) streamed += event.text;
        }
    );

    assert(result.status == EditorAiProviderStatus::Completed);
    assert(result.text == "Renderer ready.");
    assert(streamed == result.text);
    assert(result.response_id == "turn-test");
    assert(result.provider_thread_id == "thread-test");
    assert(result.provider_session_id == "thread-test");
    assert(result.input_tokens == 42);
    assert(result.output_tokens == 7);
    assert(result.total_tokens == 49);
    assert(state->started_executable == "/test/codex");
    assert(state->stopped);

    assert(state->writes.at(0).at("method") == "initialize");
    assert(state->writes.at(0).at("params").at("capabilities").at("experimentalApi") == true);
    assert(state->writes.at(1).at("method") == "initialized");
    const auto& thread = state->writes.at(2);
    assert(thread.at("method") == "thread/start");
    assert(thread.at("params").at("sandbox") == "read-only");
    assert(thread.at("params").at("approvalPolicy") == "never");
    assert(thread.at("params").at("ephemeral") == true);
    assert(thread.at("params").at("serviceName") == "engine2d_editor");
    assert(thread.at("params").at("dynamicTools").at(0).at("inputSchema").at("type") == "object");
    assert(state->writes.at(3).at("method") == "turn/start");
    assert(state->writes.at(3).at("params").at("effort") == "low");
    assert(state->writes.at(3).at("params").at("summary") == "auto");
}

void test_reasoning_summary_stream_and_item_lifecycle() {
    auto state = std::make_shared<FakeTransportState>();
    state->reads = {
        json_line({{"id", 1}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 2}, {"result", {{"thread", {{"id", "thread-reasoning"}}}}}}),
        json_line({{"id", 4}, {"result", {{"turn", {{"id", "turn-reasoning"}, {"status", "inProgress"}}}}}}),
        json_line({{"method", "item/started"}, {"params", {{"item", {
            {"type", "reasoning"}, {"id", "reasoning-1"},
            {"summary", nlohmann::json::array()}, {"content", nlohmann::json::array()}
        }}}}}),
        json_line({{"method", "item/reasoning/summaryPartAdded"}, {"params", {
            {"threadId", "thread-reasoning"}, {"turnId", "turn-reasoning"},
            {"itemId", "reasoning-1"}, {"summaryIndex", 0}
        }}}),
        json_line({{"method", "item/reasoning/summaryTextDelta"}, {"params", {
            {"threadId", "thread-reasoning"}, {"turnId", "turn-reasoning"},
            {"itemId", "reasoning-1"}, {"delta", "Inspecting the scene."},
            {"summaryIndex", 0}
        }}}),
        json_line({{"method", "item/reasoning/textDelta"}, {"params", {
            {"threadId", "thread-reasoning"}, {"turnId", "turn-reasoning"},
            {"itemId", "reasoning-1"}, {"delta", "private reasoning"},
            {"contentIndex", 0}
        }}}),
        json_line({{"method", "item/completed"}, {"params", {{"item", {
            {"type", "reasoning"}, {"id", "reasoning-1"},
            {"summary", nlohmann::json::array({"Inspecting the scene."})},
            {"content", nlohmann::json::array()}
        }}}}}),
        json_line({{"method", "turn/completed"}, {"params", {
            {"threadId", "thread-reasoning"},
            {"turn", {{"id", "turn-reasoning"}, {"status", "completed"},
                {"items", nlohmann::json::array()}}}
        }}})
    };
    CodexAppServerProvider provider(std::make_unique<FakeTransport>(state));
    std::vector<EditorAiProviderEvent> events;
    const EditorAiProviderResult result = provider.run(
        basic_request(),
        [&events](const EditorAiProviderEvent& event) { events.push_back(event); }
    );

    assert(result.status == EditorAiProviderStatus::Completed);
    const auto started = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.type == EditorAiProviderEventType::ItemStarted;
    });
    assert(started != events.end());
    assert(started->item_id == "reasoning-1");
    assert(started->item_type == "reasoning");
    const auto summary = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.type == EditorAiProviderEventType::ReasoningSummaryDelta;
    });
    assert(summary != events.end());
    assert(summary->text == "Inspecting the scene.");
    assert(std::none_of(events.begin(), events.end(), [](const auto& event) {
        return event.text == "private reasoning";
    }));
    const auto completed = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.type == EditorAiProviderEventType::ItemCompleted;
    });
    assert(completed != events.end());
    assert(completed->item_id == "reasoning-1");
    assert(completed->item_type == "reasoning");
}

void test_persistent_thread_can_be_resumed_without_history_reinjection() {
    auto start_state = standard_script();
    CodexAppServerProvider start_provider(std::make_unique<FakeTransport>(start_state));
    EditorAiProviderRequest start_request = basic_request();
    start_request.persist_thread = true;
    const EditorAiProviderResult started = start_provider.run(start_request, {});
    assert(started.status == EditorAiProviderStatus::Completed);
    assert(start_state->writes.at(2).at("method") == "thread/start");
    assert(start_state->writes.at(2).at("params").at("ephemeral") == false);

    auto resume_state = std::make_shared<FakeTransportState>();
    resume_state->reads = {
        json_line({{"id", 1}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 2}, {"result", {{"thread", {
            {"id", "thread-existing"},
            {"sessionId", "session-existing"},
            {"ephemeral", false}
        }}}}}),
        json_line({{"id", 4}, {"result", {{"turn", {{"id", "turn-resumed"}, {"status", "inProgress"}}}}}}),
        json_line({{"method", "turn/completed"}, {"params", {
            {"threadId", "thread-existing"},
            {"turn", {{"id", "turn-resumed"}, {"status", "completed"}, {"items", nlohmann::json::array()}}}
        }}})
    };
    CodexAppServerProvider resume_provider(std::make_unique<FakeTransport>(resume_state));
    EditorAiProviderRequest resume_request = basic_request();
    resume_request.provider_thread_id = "thread-existing";
    resume_request.persist_thread = true;
    resume_request.history = {{"user", "Already stored"}, {"assistant", "Do not inject"}};

    const EditorAiProviderResult resumed = resume_provider.run(resume_request, {});
    assert(resumed.status == EditorAiProviderStatus::Completed);
    assert(resumed.provider_thread_id == "thread-existing");
    assert(resumed.provider_session_id == "session-existing");
    assert(resume_state->writes.at(2).at("method") == "thread/resume");
    assert(resume_state->writes.at(2).at("params").at("threadId") == "thread-existing");
    assert(!resume_state->writes.at(2).at("params").contains("ephemeral"));
    assert(!resume_state->writes.at(2).at("params").contains("dynamicTools"));
    assert(!resume_state->writes.at(2).at("params").contains("serviceName"));
    assert(resume_state->writes.at(3).at("method") == "turn/start");
}

void test_read_only_history_and_dynamic_tool_bridge() {
    auto state = std::make_shared<FakeTransportState>();
    state->reads = {
        json_line({{"id", 1}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 2}, {"result", {{"thread", {{"id", "thread-tools"}}}}}}),
        json_line({{"id", 3}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 4}, {"result", {{"turn", {{"id", "turn-tools"}, {"status", "inProgress"}, {"items", nlohmann::json::array()}}}}}}),
        json_line({
            {"id", "tool-request"},
            {"method", "item/tool/call"},
            {"params", {
                {"threadId", "thread-tools"},
                {"turnId", "turn-tools"},
                {"callId", "call-scene"},
                {"tool", "get_scene_summary"},
                {"arguments", nlohmann::json::object()}
            }}
        }),
        json_line({{"method", "turn/completed"}, {"params", {
            {"threadId", "thread-tools"},
            {"turn", {
                {"id", "turn-tools"},
                {"status", "completed"},
                {"items", nlohmann::json::array({{{"type", "agentMessage"}, {"id", "answer"}, {"text", "One entity."}}})}
            }}
        }}})
    };
    CodexAppServerProvider provider(std::make_unique<FakeTransport>(state));
    EditorAiProviderRequest request = basic_request();
    request.history = {{"user", "What is selected?"}, {"assistant", "I will inspect it."}};
    std::size_t calls = 0;
    request.execute_tool = [&calls](const std::string& name, const nlohmann::json& arguments) {
        ++calls;
        assert(name == "get_scene_summary");
        assert(arguments == nlohmann::json::object());
        return EditorAiToolResult{
            true,
            {{"entity_count", 1}},
            {},
            "data:image/png;base64,dmlld3BvcnQ="
        };
    };

    const EditorAiProviderResult result = provider.run(request, {});
    assert(result.status == EditorAiProviderStatus::Completed);
    assert(result.text == "One entity.");
    assert(result.tool_call_count == 1);
    assert(calls == 1);
    assert(state->writes.at(2).at("params").at("sandbox") == "read-only");
    assert(state->writes.at(3).at("method") == "thread/inject_items");
    assert(state->writes.at(3).at("params").at("items").size() == 2);
    assert(state->writes.at(5).at("id") == "tool-request");
    const auto& tool_result = state->writes.at(5).at("result");
    assert(tool_result.at("success") == true);
    const nlohmann::json output = nlohmann::json::parse(
        tool_result.at("contentItems").at(0).at("text").get<std::string>()
    );
    assert(output.at("result").at("entity_count") == 1);
    assert(tool_result.at("contentItems").size() == 2);
    assert(tool_result.at("contentItems").at(1).at("type") == "inputImage");
    assert(tool_result.at("contentItems").at(1).at("imageUrl") ==
        "data:image/png;base64,dmlld3BvcnQ=");
}

void test_skill_input_and_generated_image_event() {
    auto state = std::make_shared<FakeTransportState>();
    state->reads = {
        json_line({{"id", 1}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 2}, {"result", {{"thread", {{"id", "thread-image"}}}}}}),
        json_line({{"id", 4}, {"result", {{"turn", {{"id", "turn-image"}, {"status", "inProgress"}}}}}}),
        json_line({{"method", "item/started"}, {"params", {{"item", {
            {"type", "imageGeneration"}, {"id", "image-1"}, {"status", "inProgress"}
        }}}}}),
        json_line({{"method", "item/completed"}, {"params", {{"item", {
            {"type", "imageGeneration"}, {"id", "image-1"}, {"status", "completed"},
            {"result", "ignored"}, {"savedPath", "/tmp/codex/texture.png"},
            {"transparentBackground", true}, {"failure", nullptr}
        }}}}}),
        json_line({{"method", "turn/completed"}, {"params", {
            {"threadId", "thread-image"},
            {"turn", {{"id", "turn-image"}, {"status", "completed"}, {"items", nlohmann::json::array()}}}
        }}})
    };
    CodexAppServerProvider provider(std::make_unique<FakeTransport>(state));
    EditorAiProviderRequest request = basic_request();
    request.skills.push_back({"imagegen", "/skills/imagegen/SKILL.md"});
    std::vector<EditorAiProviderEvent> events;
    const EditorAiProviderResult result = provider.run(
        request,
        [&events](const EditorAiProviderEvent& event) { events.push_back(event); }
    );

    assert(result.status == EditorAiProviderStatus::Completed);
    const auto& input = state->writes.at(3).at("params").at("input");
    assert(input.size() == 2);
    assert(input.at(0).at("type") == "skill");
    assert(input.at(0).at("name") == "imagegen");
    assert(input.at(0).at("path") == "/skills/imagegen/SKILL.md");
    assert(input.at(1).at("type") == "text");
    assert(input.at(1).at("text_elements") == nlohmann::json::array());
    const auto generated = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.type == EditorAiProviderEventType::ImageGenerated;
    });
    assert(generated != events.end());
    assert(generated->artifact_id == "image-1");
    assert(generated->artifact_path == "/tmp/codex/texture.png");
    assert(generated->transparent_background);
}

void test_failure_and_early_cancellation() {
    auto failed_state = std::make_shared<FakeTransportState>();
    failed_state->start_succeeds = false;
    CodexAppServerProvider failed_provider(std::make_unique<FakeTransport>(failed_state));
    const EditorAiProviderResult failed = failed_provider.run(basic_request(), {});
    assert(failed.status == EditorAiProviderStatus::Failed);
    assert(failed.error == "scripted start failure");

    auto cancelled_state = std::make_shared<FakeTransportState>();
    CodexAppServerProvider cancelled_provider(std::make_unique<FakeTransport>(cancelled_state));
    const EditorAiProviderResult cancelled = cancelled_provider.run(
        basic_request(), {}, []() { return true; }
    );
    assert(cancelled.status == EditorAiProviderStatus::Cancelled);
    assert(cancelled_state->started_executable.empty());
}

void test_invalid_tool_call_budget_interrupts_turn() {
    auto state = std::make_shared<FakeTransportState>();
    state->reads = {
        json_line({{"id", 1}, {"result", nlohmann::json::object()}}),
        json_line({{"id", 2}, {"result", {{"thread", {{"id", "thread-failures"}}}}}}),
        json_line({{"id", 4}, {"result", {{"turn", {{"id", "turn-failures"}, {"status", "inProgress"}}}}}}),
        json_line({
            {"id", "bad-tool-1"},
            {"method", "item/tool/call"},
            {"params", {{"tool", "get_scene_summary"}, {"arguments", nlohmann::json::object()}}}
        }),
        json_line({
            {"id", "bad-tool-2"},
            {"method", "item/tool/call"},
            {"params", {{"tool", "get_scene_summary"}, {"arguments", nlohmann::json::object()}}}
        })
    };
    CodexAppServerProvider provider(std::make_unique<FakeTransport>(state));
    EditorAiProviderRequest request = basic_request();
    request.max_tool_failures = 2;
    request.execute_tool = [](const std::string&, const nlohmann::json&) {
        return EditorAiToolResult{false, {}, "scripted invalid call"};
    };

    const EditorAiProviderResult result = provider.run(request, {});
    assert(result.status == EditorAiProviderStatus::Failed);
    assert(result.error == "Codex produced too many invalid editor tool calls");
    assert(result.tool_call_count == 2);
    assert(state->writes.at(6).at("method") == "turn/interrupt");
    assert(state->writes.at(6).at("params").at("turnId") == "turn-failures");
}

void test_live_app_server_when_requested() {
    const char* enabled = std::getenv("ENGINE_RUN_CODEX_APP_SERVER_SMOKE");
    if (!enabled || std::string(enabled) != "1") return;

    CodexAppServerProvider provider(create_default_codex_app_server_transport());
    EditorAiProviderRequest request = basic_request();
    request.project_root = std::filesystem::current_path().string();
    request.codex_executable = find_default_codex_executable();
    request.prompt = "Call get_scene_summary and reply only ENGINE2D_SDL_CODEX_READY.";
    request.execute_tool = [](const std::string& name, const nlohmann::json& arguments) {
        assert(name == "get_scene_summary");
        assert(arguments == nlohmann::json::object());
        return EditorAiToolResult{true, {{"entity_count", 1}}, {}};
    };

    const EditorAiProviderResult result = provider.run(request, {});
    assert(result.status == EditorAiProviderStatus::Completed);
    assert(result.tool_call_count == 1);
    assert(result.text.find("ENGINE2D_SDL_CODEX_READY") != std::string::npos);
}

} // namespace

int main() {
    test_subscription_protocol_streaming_and_read_only_sandbox();
    test_reasoning_summary_stream_and_item_lifecycle();
    test_read_only_history_and_dynamic_tool_bridge();
    test_skill_input_and_generated_image_event();
    test_persistent_thread_can_be_resumed_without_history_reinjection();
    test_failure_and_early_cancellation();
    test_invalid_tool_call_budget_interrupts_turn();
    test_live_app_server_when_requested();
    std::cout << "Codex App Server provider tests passed\n";
    return 0;
}

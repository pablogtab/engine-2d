#include "editor/ai/EditorAiActivityFormatter.hpp"

#include <cassert>
#include <iostream>

namespace {

using namespace Engine;

void test_null_command_fields_are_safe() {
    EditorAiProviderEvent event;
    event.type = EditorAiProviderEventType::ItemStarted;
    event.item_type = "commandExecution";
    event.item_status = "inProgress";
    event.details = {
        {"command", "cmake --build build"},
        {"cwd", nullptr},
        {"aggregatedOutput", nullptr},
        {"exitCode", nullptr},
        {"durationMs", nullptr}
    };

    const EditorAiActivityPresentation presentation = format_editor_ai_activity(event);

    assert(presentation.summary == "Running cmake --build build");
    assert(presentation.detail == "$ cmake --build build");
}

void test_unexpected_types_fall_back_without_throwing() {
    EditorAiProviderEvent event;
    event.type = EditorAiProviderEventType::ItemCompleted;
    event.item_type = "commandExecution";
    event.item_status = "completed";
    event.details = {
        {"command", nlohmann::json::array({"cmake", "--build", "build"})},
        {"cwd", 42},
        {"aggregatedOutput", nlohmann::json::object()},
        {"exitCode", 0},
        {"durationMs", 15}
    };

    const EditorAiActivityPresentation presentation = format_editor_ai_activity(event);

    assert(presentation.summary == "Ran command");
    assert(presentation.detail.find("Exit code: 0") != std::string::npos);
    assert(presentation.detail.find("Duration: 15 ms") != std::string::npos);
}

void test_null_dynamic_tool_payloads_are_safe() {
    EditorAiProviderEvent event;
    event.type = EditorAiProviderEventType::ItemCompleted;
    event.item_type = "dynamicToolCall";
    event.item_status = "failed";
    event.tool_name = "find_scene_entities";
    event.details = {
        {"tool", nullptr},
        {"arguments", nullptr},
        {"result", nullptr},
        {"error", nullptr}
    };

    const EditorAiActivityPresentation presentation = format_editor_ai_activity(event);

    assert(presentation.summary == "Used find_scene_entities");
    assert(presentation.detail == "Tool: find_scene_entities");
}

void test_dynamic_tool_lifecycle_distinguishes_waiting_from_execution() {
    EditorAiProviderEvent waiting;
    waiting.type = EditorAiProviderEventType::ItemStarted;
    waiting.item_type = "dynamicToolCall";
    waiting.item_status = "inProgress";
    waiting.details = {{"tool", "propose_configure_component"}};
    assert(format_editor_ai_activity(waiting).summary ==
        "Waiting to call propose_configure_component");

    EditorAiProviderEvent executing = waiting;
    executing.type = EditorAiProviderEventType::ToolStarted;
    assert(format_editor_ai_activity(executing).summary ==
        "Executing propose_configure_component");
}

} // namespace

int main() {
    test_null_command_fields_are_safe();
    test_unexpected_types_fall_back_without_throwing();
    test_null_dynamic_tool_payloads_are_safe();
    test_dynamic_tool_lifecycle_distinguishes_waiting_from_execution();
    std::cout << "Editor AI activity formatter tests passed\n";
    return 0;
}

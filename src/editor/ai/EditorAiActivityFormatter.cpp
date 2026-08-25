#include "EditorAiActivityFormatter.hpp"

#include <algorithm>

namespace Engine {
namespace {

std::string item_label(const std::string& item_type) {
    if (item_type == "reasoning") return "Reasoning";
    if (item_type == "agentMessage") return "Writing response";
    if (item_type == "imageGeneration") return "Generating image";
    if (item_type == "dynamicToolCall") return "Using editor tool";
    if (item_type == "webSearch") return "Searching the web";
    if (item_type == "commandExecution") return "Running command";
    if (item_type == "fileChange") return "Preparing file changes";
    if (item_type == "contextCompaction") return "Compacting context";
    return item_type.empty() ? "Codex activity" : item_type;
}

std::string single_line(std::string text, std::size_t limit = 96) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    while (text.find("  ") != std::string::npos) text.replace(text.find("  "), 2, " ");
    if (text.size() > limit) text = text.substr(0, limit - 3) + "...";
    return text;
}

std::string json_string(
    const nlohmann::json& object,
    const char* key,
    std::string fallback = {}
) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return fallback;
    }
    return object.at(key).get<std::string>();
}

std::string summary(const EditorAiProviderEvent& event) {
    const nlohmann::json& item = event.details;
    if (event.item_type == "commandExecution") {
        const std::string command = json_string(item, "command", "command");
        return (event.item_status == "inProgress" ? "Running " : "Ran ") +
            single_line(command);
    }
    if (event.item_type == "dynamicToolCall") {
        const std::string tool = json_string(item, "tool", event.tool_name);
        if (event.type == EditorAiProviderEventType::ItemStarted) {
            return "Waiting to call " + tool;
        }
        if (event.type == EditorAiProviderEventType::ToolStarted) {
            return "Executing " + tool;
        }
        return "Used " + tool;
    }
    if (event.item_type == "fileChange") {
        const std::size_t count = item.contains("changes") && item.at("changes").is_array()
            ? item.at("changes").size() : 0;
        return count == 1 ? "Edited 1 file" : "Edited " + std::to_string(count) + " files";
    }
    if (event.item_type == "webSearch") {
        return "Searched " + single_line(json_string(item, "query", "the web"));
    }
    if (event.item_type == "imageGeneration") return "Generated image";
    if (event.item_type == "contextCompaction") return "Compacted conversation context";
    if (event.item_type == "reasoning") return "Analyzed the request";
    return item_label(event.item_type);
}

std::string detail(const EditorAiProviderEvent& event) {
    if (!event.details.is_object() || event.details.empty()) return event.text;
    const auto& item = event.details;
    std::string output;
    if (event.item_type == "commandExecution") {
        output = "$ " + json_string(item, "command");
        const std::string cwd = json_string(item, "cwd");
        if (!cwd.empty()) output += "\n\nWorking directory: " + cwd;
        if (item.contains("exitCode") && item.at("exitCode").is_number_integer()) {
            output += "\nExit code: " + std::to_string(item.at("exitCode").get<int>());
        }
        if (item.contains("durationMs") && item.at("durationMs").is_number_integer()) {
            output += "\nDuration: " +
                std::to_string(item.at("durationMs").get<std::uint64_t>()) + " ms";
        }
        const std::string aggregated_output = json_string(item, "aggregatedOutput");
        if (!aggregated_output.empty()) output += "\n\nOutput\n" + aggregated_output;
        return output;
    }
    if (event.item_type == "dynamicToolCall") {
        output = "Tool: " + json_string(item, "tool", event.tool_name);
        if (item.contains("arguments") && !item.at("arguments").is_null()) {
            output += "\n\nArguments\n" + item.at("arguments").dump(2);
        }
        if (item.contains("result") && !item.at("result").is_null()) {
            output += "\n\nResult\n" + item.at("result").dump(2);
        }
        if (item.contains("error") && !item.at("error").is_null()) {
            output += "\n\nError\n" + (item.at("error").is_string()
                ? item.at("error").get<std::string>() : item.at("error").dump(2));
        }
        return output;
    }
    if (event.item_type == "fileChange" && item.contains("changes") &&
        item.at("changes").is_array()) {
        for (const auto& change : item.at("changes")) {
            if (!output.empty()) output += "\n\n";
            output += json_string(change, "kind", "change") + ": " +
                json_string(change, "path", "unknown");
            const std::string diff = json_string(change, "diff");
            if (!diff.empty()) output += "\n" + diff;
        }
        return output;
    }
    return item.dump(2);
}

} // namespace

EditorAiActivityPresentation format_editor_ai_activity(
    const EditorAiProviderEvent& event
) {
    return {summary(event), detail(event)};
}

} // namespace Engine

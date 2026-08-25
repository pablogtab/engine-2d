#include "ProjectCommandRegistry.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_process.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

namespace Engine {
namespace {
constexpr std::size_t k_max_output_bytes = 64 * 1024;
bool allowed_kind(const std::string& kind) {
    return kind == "build" || kind == "test" || kind == "format" || kind == "export" || kind == "log";
}
std::string expand(std::string value, const std::string& project, const std::string& engine, const std::string& build) {
    const auto replace_all = [&value](const std::string& from, const std::string& to) {
        for (std::size_t at = 0; (at = value.find(from, at)) != std::string::npos; at += to.size()) value.replace(at, from.size(), to);
    };
    replace_all("${projectRoot}", project); replace_all("${engineRoot}", engine); replace_all("${buildDir}", build);
    return value;
}
bool contained_by(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto canonical_child = std::filesystem::weakly_canonical(child);
    const auto canonical_parent = std::filesystem::weakly_canonical(parent);
    auto mismatch = std::mismatch(canonical_parent.begin(), canonical_parent.end(), canonical_child.begin(), canonical_child.end());
    return mismatch.first == canonical_parent.end();
}
}

ProjectCommandRegistry::ProjectCommandRegistry(std::string project_root, std::string engine_root)
    : m_project_root(std::move(project_root)), m_engine_root(std::move(engine_root)) {}

bool ProjectCommandRegistry::load(std::string& error) {
    m_commands.clear();
    const std::filesystem::path path = std::filesystem::path(m_project_root) / "ProjectSettings/Commands.json";
    std::ifstream input(path); if (!input) { error = "ProjectSettings/Commands.json is unavailable"; return false; }
    nlohmann::json root;
    try { input >> root; } catch (const std::exception& exception) { error = exception.what(); return false; }
    if (!root.is_object() || root.value("version", 0) != 1 || !root.contains("commands") || !root["commands"].is_array()) { error = "Commands.json must use schema version 1"; return false; }
    std::set<std::string> names;
    for (const auto& item : root["commands"]) {
        if (!item.is_object() || !item.contains("name") || !item.contains("kind") || !item.contains("program") || !item.contains("args") || !item.contains("working_directory")) { error = "Command is missing required fields"; return false; }
        ProjectCommand command{item.value("name", ""), item.value("kind", ""), item.value("program", ""), item.value("args", std::vector<std::string>{}), item.value("working_directory", ""), item.value("timeout_seconds", 300), item.value("artifact_patterns", std::vector<std::string>{})};
        if (command.name.empty() || !allowed_kind(command.kind) || !names.insert(command.name).second || command.timeout_seconds < 1 || command.timeout_seconds > 1800 || command.program.empty() || command.program.find('\n') != std::string::npos) { error = "Command has an invalid name, kind, program, or timeout"; return false; }
        std::vector<std::string> expanded; std::string cwd;
        if (!expand_and_validate(command, expanded, cwd, error)) return false;
        m_commands.push_back(std::move(command));
    }
    return true;
}

nlohmann::json ProjectCommandRegistry::status() const {
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& command : m_commands) commands.push_back({{"name", command.name}, {"kind", command.kind}, {"timeout_seconds", command.timeout_seconds}});
    return {{"schema_version", 1}, {"commands", std::move(commands)}, {"running", false}};
}

bool ProjectCommandRegistry::expand_and_validate(const ProjectCommand& command, std::vector<std::string>& arguments, std::string& cwd, std::string& error) const {
    const std::string build = (std::filesystem::path(m_engine_root) / "build").string();
    cwd = expand(command.working_directory, m_project_root, m_engine_root, build);
    if (!contained_by(cwd, m_project_root) && !contained_by(cwd, m_engine_root)) { error = "Command working directory escapes allowed roots"; return false; }
    const std::string program = expand(command.program, m_project_root, m_engine_root, build);
    if (program.find("${") != std::string::npos || program.find_first_of(";|&`\n") != std::string::npos) { error = "Command program must be a direct executable, not shell syntax"; return false; }
    arguments = {program};
    for (const std::string& arg : command.args) {
        if (arg.find('\n') != std::string::npos) { error = "Command argument contains a newline"; return false; }
        const std::string value = expand(arg, m_project_root, m_engine_root, build);
        if (value.find("${") != std::string::npos) { error = "Command argument has an unknown placeholder"; return false; }
        arguments.push_back(value);
    }
    return true;
}

bool ProjectCommandRegistry::run(const std::string& name, nlohmann::json& result, std::string& error) const {
    const auto found = std::find_if(m_commands.begin(), m_commands.end(), [&name](const ProjectCommand& command) { return command.name == name; });
    if (found == m_commands.end()) { error = "Unknown allowlisted command"; return false; }
    std::vector<std::string> arguments; std::string cwd;
    if (!expand_and_validate(*found, arguments, cwd, error)) return false;
    std::vector<const char*> argv; for (const std::string& argument : arguments) argv.push_back(argument.c_str()); argv.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props); SDL_DestroyProperties(props);
    if (!process) { error = SDL_GetError(); return false; }
    const auto started = std::chrono::steady_clock::now(); int exit_code = -255; bool timed_out = false;
    while (!SDL_WaitProcess(process, false, &exit_code)) {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(found->timeout_seconds)) { SDL_KillProcess(process, true); timed_out = true; SDL_WaitProcess(process, true, &exit_code); break; }
        SDL_Delay(10);
    }
    size_t bytes = 0; int read_exit = exit_code; void* data = SDL_ReadProcess(process, &bytes, &read_exit);
    std::string output; if (data && bytes) output.assign(static_cast<const char*>(data), std::min(bytes, k_max_output_bytes)); SDL_free(data);
    SDL_DestroyProcess(process);
    result = {{"id", name}, {"name", name}, {"status", timed_out ? "timed_out" : (exit_code == 0 ? "succeeded" : "failed")}, {"exit_code", exit_code}, {"duration_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()}, {"output", output}, {"output_truncated", bytes > k_max_output_bytes}, {"artifacts", nlohmann::json::array()}};
    return true;
}

} // namespace Engine

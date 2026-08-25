#include "ProjectCommandRegistry.hpp"
#include "../../core/Log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_process.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <atomic>
#include <mutex>
#include <thread>

namespace Engine {
namespace {
constexpr std::size_t k_max_output_bytes = 64 * 1024;
std::mutex g_process_mutex;
SDL_Process* g_active_process{nullptr};
std::string g_active_command;
std::string g_active_id;
std::string g_active_status{"idle"};
nlohmann::json g_last_result = nlohmann::json::object();
std::atomic_bool g_command_running{false};
std::atomic_bool g_cancel_requested{false};
std::atomic_uint64_t g_next_command_id{1};
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
    const std::filesystem::path build = std::filesystem::path(m_engine_root) / "build";
    nlohmann::json executables = nlohmann::json::array();
    for (const char* name : {"Engine2D", "Engine2DStandalone", "e2sc", "e2s-lsp"}) {
        const std::filesystem::path candidate = build / name;
        if (std::filesystem::is_regular_file(candidate)) executables.push_back(candidate.string());
    }
#if defined(__APPLE__)
    constexpr const char* platform = "macos";
#elif defined(_WIN32)
    constexpr const char* platform = "windows";
#else
    constexpr const char* platform = "linux";
#endif
    std::lock_guard lock(g_process_mutex);
    return {{"schema_version", 1}, {"platform", platform}, {"build_directory", build.string()},
            {"executables", std::move(executables)}, {"commands", std::move(commands)},
            {"running", g_command_running.load() && g_active_process != nullptr}, {"active_command", g_active_command},
            {"active_id", g_active_id}, {"active_status", g_active_status},
            {"last_result", g_last_result}};
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
    for (const std::string& pattern : command.artifact_patterns) {
        const std::string value = expand(pattern, m_project_root, m_engine_root, build);
        if (value.find("${") != std::string::npos ||
            (!contained_by(value, m_project_root) && !contained_by(value, build))) {
            error = "Command artifact escapes project/build roots or has an unknown placeholder";
            return false;
        }
    }
    return true;
}

bool ProjectCommandRegistry::run(const std::string& name, nlohmann::json& result, std::string& error) const {
    bool expected = false;
    if (!g_command_running.compare_exchange_strong(expected, true)) {
        error = "Another project command is already running";
        return false;
    }
    struct RunningGuard {
        ~RunningGuard() {
            std::lock_guard lock(g_process_mutex);
            g_active_process = nullptr;
            g_active_command.clear();
            g_active_id.clear();
            g_active_status = "idle";
            g_command_running.store(false);
            g_cancel_requested.store(false);
        }
    } guard;
    const auto found = std::find_if(m_commands.begin(), m_commands.end(), [&name](const ProjectCommand& command) { return command.name == name; });
    if (found == m_commands.end()) { error = "Unknown allowlisted command"; return false; }
    std::vector<std::string> arguments; std::string cwd;
    if (!expand_and_validate(*found, arguments, cwd, error)) return false;
    std::vector<const char*> argv; for (const std::string& argument : arguments) argv.push_back(argument.c_str()); argv.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props); SDL_DestroyProperties(props);
    if (!process) { error = SDL_GetError(); return false; }
    const std::string command_id = "command-" + std::to_string(g_next_command_id.fetch_add(1));
    {
        std::lock_guard lock(g_process_mutex);
        g_active_process = process; g_active_command = name; g_active_id = command_id;
        g_active_status = "running"; g_cancel_requested.store(false);
    }
    ENGINE_LOG_INFO("[AI AUDIT] phase=project_command_start id={} name={} cwd={}", command_id, name, cwd);
    SDL_IOStream* stream = SDL_GetProcessOutput(process);
    const auto started = std::chrono::steady_clock::now();
    int exit_code = -255; bool timed_out = false; bool cancelled = false; bool exited = false;
    std::string output; std::size_t total_output_bytes = 0;
    const auto drain_output = [&]() {
        if (!stream) return;
        char buffer[4096];
        // Bound each drain pass so a process with endless output cannot starve
        // timeout/cancellation polling.
        for (int chunk = 0; chunk < 16; ++chunk) {
            const std::size_t read = SDL_ReadIO(stream, buffer, sizeof(buffer));
            if (read == 0) break;
            total_output_bytes += read;
            const std::size_t remaining = output.size() < k_max_output_bytes ? k_max_output_bytes - output.size() : 0;
            output.append(buffer, std::min(read, remaining));
        }
    };
    while (!exited) {
        drain_output();
        {
            std::lock_guard lock(g_process_mutex);
            exited = SDL_WaitProcess(process, false, &exit_code);
            cancelled = g_cancel_requested.load();
            if (!exited && !cancelled && std::chrono::steady_clock::now() - started > std::chrono::seconds(found->timeout_seconds)) {
                SDL_KillProcess(process, true); timed_out = true; g_active_status = "timed_out";
            }
        }
        if (!exited) SDL_Delay(10);
    }
    drain_output();

    nlohmann::json artifacts = nlohmann::json::array();
    const std::string build = (std::filesystem::path(m_engine_root) / "build").string();
    for (const std::string& pattern : found->artifact_patterns) {
        const std::filesystem::path artifact = expand(pattern, m_project_root, m_engine_root, build);
        if ((contained_by(artifact, m_project_root) || contained_by(artifact, build)) && std::filesystem::exists(artifact)) {
            artifacts.push_back({{"path", artifact.string()}, {"directory", std::filesystem::is_directory(artifact)}});
        }
    }
    nlohmann::json diagnostics = nlohmann::json::array();
    std::size_t line_start = 0;
    while (line_start < output.size() && diagnostics.size() < 100) {
        const std::size_t line_end = output.find('\n', line_start);
        const std::string line = output.substr(line_start, line_end == std::string::npos ? std::string::npos : line_end - line_start);
        if (line.find("error:") != std::string::npos || line.find("FAILED") != std::string::npos) diagnostics.push_back(line);
        if (line_end == std::string::npos) break;
        line_start = line_end + 1;
    }
    SDL_DestroyProcess(process);
    const std::string status = timed_out ? "timed_out" : cancelled ? "cancelled" : (exit_code == 0 ? "succeeded" : "failed");
    result = {{"id", command_id}, {"name", name}, {"status", status}, {"exit_code", exit_code},
              {"duration_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()},
              {"cancelled", cancelled}, {"timed_out", timed_out}, {"output", output},
              {"output_truncated", total_output_bytes > k_max_output_bytes}, {"diagnostics", std::move(diagnostics)},
              {"artifacts", std::move(artifacts)}};
    {
        std::lock_guard lock(g_process_mutex);
        g_last_result = result;
        g_active_status = status;
        g_active_process = nullptr;
    }
    ENGINE_LOG_INFO(
        "[AI AUDIT] phase=project_command_complete id={} name={} status={} exit_code={} duration_ms={} artifacts={} truncated={}",
        command_id, name, status, exit_code, result.at("duration_ms").get<long long>(),
        result.at("artifacts").size(), result.at("output_truncated").get<bool>()
    );
    for (const auto& artifact : result.at("artifacts")) {
        ENGINE_LOG_INFO("[AI AUDIT] phase=project_command_artifact id={} name={} path={}",
            command_id, name, artifact.at("path").get<std::string>());
    }
    return true;
}

bool ProjectCommandRegistry::cancel_active() {
    std::lock_guard lock(g_process_mutex);
    if (!g_active_process || !g_command_running.load()) return false;
    g_cancel_requested.store(true);
    g_active_status = "cancelling";
    ENGINE_LOG_INFO("[AI AUDIT] phase=project_command_cancel id={} name={}", g_active_id, g_active_command);
    return SDL_KillProcess(g_active_process, false) || SDL_KillProcess(g_active_process, true);
}

} // namespace Engine

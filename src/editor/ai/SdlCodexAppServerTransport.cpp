#include "CodexAppServerProvider.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <vector>

namespace Engine {
namespace {

bool is_executable_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

std::string platform_codex_relative_path() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "bin/macos-aarch64/codex";
#elif defined(__APPLE__)
    return "bin/macos-x86_64/codex";
#elif defined(_WIN32)
    return "bin/windows-x86_64/codex.exe";
#elif defined(__aarch64__)
    return "bin/linux-aarch64/codex";
#else
    return "bin/linux-x86_64/codex";
#endif
}

class SdlCodexAppServerTransport final : public ICodexAppServerTransport {
public:
    ~SdlCodexAppServerTransport() override { stop(); }

    bool start(const std::string& executable, std::string& error) override {
        stop();
        if (executable.empty()) {
            error = "Codex executable was not found. Install Codex or set CODEX_EXECUTABLE.";
            return false;
        }
        const char* args[] = {executable.c_str(), "app-server", "--stdio", nullptr};
        m_process = SDL_CreateProcess(args, true);
        if (!m_process) {
            error = "Could not start Codex App Server: " + std::string(SDL_GetError());
            return false;
        }
        m_input = SDL_GetProcessInput(m_process);
        m_output = SDL_GetProcessOutput(m_process);
        if (!m_input || !m_output) {
            error = "Could not open Codex App Server stdio: " + std::string(SDL_GetError());
            stop();
            return false;
        }
        m_read_buffer.clear();
        return true;
    }

    bool write_line(const std::string& line, std::string& error) override {
        if (!m_input) {
            error = "Codex App Server input is closed";
            return false;
        }
        const std::string payload = line + "\n";
        std::size_t written = 0;
        constexpr auto WRITE_TIMEOUT = std::chrono::seconds(8);
        const auto deadline = std::chrono::steady_clock::now() + WRITE_TIMEOUT;
        while (written < payload.size()) {
            const std::size_t count = SDL_WriteIO(
                m_input, payload.data() + written, payload.size() - written
            );
            if (count == 0) {
                const SDL_IOStatus status = SDL_GetIOStatus(m_input);
                if (status == SDL_IO_STATUS_NOT_READY || status == SDL_IO_STATUS_READY) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        error = "Timed out writing " + std::to_string(payload.size()) +
                            " bytes to Codex App Server after " + std::to_string(written) +
                            " bytes";
                        return false;
                    }
                    SDL_Delay(2);
                    continue;
                }
                const std::string detail = SDL_GetError();
                error = "Could not write to Codex App Server (status " +
                    std::to_string(static_cast<int>(status)) + ", " +
                    std::to_string(written) + "/" + std::to_string(payload.size()) +
                    " bytes)";
                if (!detail.empty()) error += ": " + detail;
                return false;
            }
            written += count;
        }
        // SDL process stdin is an OS pipe. SDL_WriteIO transfers bytes directly to that pipe;
        // SDL_FlushIO may map to an unsupported fsync-style operation and report failure after
        // the complete JSONL message was already written.
        return true;
    }

    CodexAppServerReadResult read_line(
        const std::function<bool()>& is_cancelled
    ) override {
        while (true) {
            const std::size_t newline = m_read_buffer.find('\n');
            if (newline != std::string::npos) {
                std::string line = m_read_buffer.substr(0, newline);
                m_read_buffer.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                return {CodexAppServerReadStatus::Line, std::move(line), {}};
            }
            if (is_cancelled && is_cancelled()) {
                return {CodexAppServerReadStatus::Cancelled, {}, {}};
            }
            if (!m_output) {
                return {CodexAppServerReadStatus::Closed, {}, "Codex App Server output is closed"};
            }

            char chunk[4096];
            const std::size_t count = SDL_ReadIO(m_output, chunk, sizeof(chunk));
            if (count > 0) {
                m_read_buffer.append(chunk, count);
                continue;
            }
            const SDL_IOStatus status = SDL_GetIOStatus(m_output);
            if (status == SDL_IO_STATUS_NOT_READY || status == SDL_IO_STATUS_READY) {
                SDL_Delay(10);
                continue;
            }
            if (status == SDL_IO_STATUS_EOF) {
                if (!m_read_buffer.empty()) {
                    std::string line = std::move(m_read_buffer);
                    m_read_buffer.clear();
                    return {CodexAppServerReadStatus::Line, std::move(line), {}};
                }
                return {CodexAppServerReadStatus::Closed, {}, "Codex App Server exited"};
            }
            return {
                CodexAppServerReadStatus::Failed,
                {},
                "Could not read Codex App Server output: " + std::string(SDL_GetError())
            };
        }
    }

    void stop() override {
        m_input = nullptr;
        m_output = nullptr;
        m_read_buffer.clear();
        if (!m_process) return;
        int exit_code = 0;
        if (!SDL_WaitProcess(m_process, false, &exit_code)) {
            SDL_KillProcess(m_process, false);
            SDL_Delay(10);
            if (!SDL_WaitProcess(m_process, false, &exit_code)) {
                SDL_KillProcess(m_process, true);
                SDL_WaitProcess(m_process, true, &exit_code);
            }
        }
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
    }

private:
    SDL_Process* m_process{nullptr};
    SDL_IOStream* m_input{nullptr};
    SDL_IOStream* m_output{nullptr};
    std::string m_read_buffer;
};

} // namespace

std::string find_default_codex_executable() {
    if (const char* configured = std::getenv("CODEX_EXECUTABLE")) {
        if (configured[0] != '\0') return configured;
    }

    if (const char* path_value = std::getenv("PATH")) {
#if defined(_WIN32)
        constexpr char separator = ';';
        constexpr const char* executable_name = "codex.exe";
#else
        constexpr char separator = ':';
        constexpr const char* executable_name = "codex";
#endif
        std::stringstream paths(path_value);
        std::string entry;
        while (std::getline(paths, entry, separator)) {
            const std::filesystem::path candidate = std::filesystem::path(entry) / executable_name;
            if (is_executable_file(candidate)) return candidate.string();
        }
    }

    const char* home_value = std::getenv("HOME");
    if (!home_value || home_value[0] == '\0') return {};
    const std::filesystem::path home(home_value);
    const std::vector<std::filesystem::path> direct_candidates{
        home / ".codex" / "bin" / "codex",
        home / ".local" / "bin" / "codex"
    };
    for (const auto& candidate : direct_candidates) {
        if (is_executable_file(candidate)) return candidate.string();
    }

    const std::filesystem::path extensions = home / ".vscode" / "extensions";
    std::error_code error;
    if (!std::filesystem::is_directory(extensions, error)) return {};
    std::filesystem::path newest;
    for (const auto& entry : std::filesystem::directory_iterator(extensions, error)) {
        if (error || !entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("openai.chatgpt-")) continue;
        const std::filesystem::path candidate = entry.path() / platform_codex_relative_path();
        if (is_executable_file(candidate) && (newest.empty() || candidate.string() > newest.string())) {
            newest = candidate;
        }
    }
    return newest.string();
}

std::string find_default_codex_skill_path(const std::string& skill_name) {
    if (skill_name.empty() || skill_name.find('/') != std::string::npos ||
        skill_name.find('\\') != std::string::npos || skill_name == "." || skill_name == "..") {
        return {};
    }
    std::vector<std::filesystem::path> roots;
    if (const char* codex_home = std::getenv("CODEX_HOME")) {
        if (codex_home[0] != '\0') roots.emplace_back(codex_home);
    }
    if (const char* home_value = std::getenv("HOME")) {
        if (home_value[0] != '\0') roots.emplace_back(std::filesystem::path(home_value) / ".codex");
    }
    for (const std::filesystem::path& root : roots) {
        for (const std::filesystem::path& candidate : {
                 root / "skills" / ".system" / skill_name / "SKILL.md",
                 root / "skills" / skill_name / "SKILL.md"}) {
            if (is_executable_file(candidate)) return candidate.string();
        }
    }
    return {};
}

std::unique_ptr<ICodexAppServerTransport> create_default_codex_app_server_transport() {
    return std::make_unique<SdlCodexAppServerTransport>();
}

} // namespace Engine

#pragma once

#include <string>
#include <functional>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>

namespace Engine {

enum class LogLevel {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERR,
    CRITICAL,
    OFF
};

struct LogSourceLocation {
    std::string path;
    uint32_t line{0};
    uint32_t column{0};

    [[nodiscard]] bool is_valid() const { return !path.empty() && line != 0; }
};

struct LogMetadata {
    std::string category;
    std::string system;
    std::string render_pass;
    std::optional<uint32_t> entity;
    std::optional<uint64_t> asset;
    std::optional<LogSourceLocation> source;
};

struct LogRecord {
    std::string timestamp;
    LogLevel level{LogLevel::INFO};
    std::string message;
    LogMetadata metadata;
};

class Log {
public:
    using LogCallback = std::function<void(const LogRecord& record)>;

    static void init(const std::string& filename = "engine.log");
    static void shutdown();
    static void set_level(LogLevel level);
    static void set_callback(LogCallback callback);

    static void trace(const std::string& msg);
    static void debug(const std::string& msg);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void error(std::string msg, LogMetadata metadata);
    static void critical(const std::string& msg);

    // Formatted helper for multiple arguments
    template <typename... Args>
    static void trace(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::TRACE, format_msg(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void debug(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::DEBUG, format_msg(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void info(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::INFO, format_msg(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void warn(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::WARN, format_msg(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void error(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::ERR, format_msg(fmt_str, std::forward<Args>(args)...));
    }

private:
    static void log(LogLevel level, const std::string& message, LogMetadata metadata = {});
    static std::string get_timestamp();
    static const char* level_to_string(LogLevel level);

    template <typename T>
    static void format_helper(std::ostringstream& ss, const std::string& fmt, size_t& pos, T&& val) {
        size_t open_brace = fmt.find('{', pos);
        if (open_brace != std::string::npos) {
            size_t close_brace = fmt.find('}', open_brace);
            if (close_brace != std::string::npos) {
                ss << fmt.substr(pos, open_brace - pos);
                ss << std::forward<T>(val);
                pos = close_brace + 1;
            }
        }
    }

    template <typename... Args>
    static std::string format_msg(const std::string& fmt, Args&&... args) {
        std::ostringstream ss;
        size_t pos = 0;
        (format_helper(ss, fmt, pos, std::forward<Args>(args)), ...);
        if (pos < fmt.length()) {
            ss << fmt.substr(pos);
        }
        return ss.str();
    }

    static LogLevel s_level;
    static std::ofstream s_file_stream;
    static std::mutex s_mutex;
    static bool s_initialized;
};

} // namespace Engine

// Global Log Macros
#ifdef NDEBUG
    #define ENGINE_LOG_TRACE(...) ((void)0)
    #define ENGINE_LOG_DEBUG(...) ((void)0)
#else
    #define ENGINE_LOG_TRACE(...) ::Engine::Log::trace(__VA_ARGS__)
    #define ENGINE_LOG_DEBUG(...) ::Engine::Log::debug(__VA_ARGS__)
#endif

#define ENGINE_LOG_INFO(...)     ::Engine::Log::info(__VA_ARGS__)
#define ENGINE_LOG_WARN(...)     ::Engine::Log::warn(__VA_ARGS__)
#define ENGINE_LOG_ERROR(...)    ::Engine::Log::error(__VA_ARGS__)
#define ENGINE_LOG_CRITICAL(...) ::Engine::Log::critical(__VA_ARGS__)

#include "Log.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace Engine {

LogLevel Log::s_level = LogLevel::TRACE;
std::ofstream Log::s_file_stream;
std::mutex Log::s_mutex;
bool Log::s_initialized = false;
static Log::LogCallback s_log_callback = nullptr;

void Log::set_callback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_log_callback = std::move(callback);
}

void Log::init(const std::string& filename) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized) return;

    s_file_stream.open(filename, std::ios::out | std::ios::trunc);
    s_initialized = true;

    // Internal log bootstrap
    std::cout << "\033[32m[ENGINE LOG] Log system initialized. Output file: " << filename << "\033[0m" << std::endl;
    if (s_file_stream.is_open()) {
        s_file_stream << "[" << get_timestamp() << "] [INFO] Engine Log System Initialized.\n";
    }
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized) return;

    if (s_file_stream.is_open()) {
        s_file_stream << "[" << get_timestamp() << "] [INFO] Engine Log System Shutdown.\n";
        s_file_stream.close();
    }
    s_initialized = false;
}

void Log::set_level(LogLevel level) {
    s_level = level;
}

std::string Log::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

const char* Log::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO ";
        case LogLevel::WARN:     return "WARN ";
        case LogLevel::ERR:      return "ERROR";
        case LogLevel::CRITICAL: return "CRIT ";
        default:                 return "LOG  ";
    }
}

void Log::log(LogLevel level, const std::string& message, LogMetadata metadata) {
    if (level < s_level) return;

    std::lock_guard<std::mutex> lock(s_mutex);

    std::string timestamp = get_timestamp();
    const char* lvl_str = level_to_string(level);

    // ANSI terminal color codes
    const char* color_code = "\033[0m";
    switch (level) {
        case LogLevel::TRACE:    color_code = "\033[90m"; break;   // Gray
        case LogLevel::DEBUG:    color_code = "\033[36m"; break;   // Cyan
        case LogLevel::INFO:     color_code = "\033[32m"; break;   // Green
        case LogLevel::WARN:     color_code = "\033[33m"; break;   // Yellow
        case LogLevel::ERR:      color_code = "\033[31m"; break;   // Red
        case LogLevel::CRITICAL: color_code = "\033[1;31m"; break; // Bold Red
        default: break;
    }

    LogRecord record{timestamp, level, message, std::move(metadata)};
    std::string attribution;
    if (!record.metadata.category.empty()) attribution += " [" + record.metadata.category + "]";
    if (record.metadata.entity) attribution += " [entity=" + std::to_string(*record.metadata.entity) + "]";
    if (record.metadata.source && record.metadata.source->is_valid()) {
        attribution += " [" + record.metadata.source->path + ":" +
            std::to_string(record.metadata.source->line) + ":" +
            std::to_string(record.metadata.source->column) + "]";
    }

    // 1. Terminal Console Output
    std::cout << "\033[90m[" << timestamp << "]\033[0m "
              << color_code << "[" << lvl_str << "]\033[0m "
              << attribution << " " << message << std::endl;

    // 2. File Stream Output
    if (s_file_stream.is_open()) {
        s_file_stream << "[" << timestamp << "] [" << lvl_str << "]" << attribution << " " << message << "\n";
        s_file_stream.flush();
    }

    // 3. In-Editor Console Callback
    if (s_log_callback) {
        s_log_callback(record);
    }
}

void Log::trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
void Log::debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
void Log::info(const std::string& msg)  { log(LogLevel::INFO, msg); }
void Log::warn(const std::string& msg)  { log(LogLevel::WARN, msg); }
void Log::error(const std::string& msg) { log(LogLevel::ERR, msg); }
void Log::error(std::string msg, LogMetadata metadata) { log(LogLevel::ERR, msg, std::move(metadata)); }
void Log::critical(const std::string& msg) { log(LogLevel::CRITICAL, msg); }

} // namespace Engine
